// POSIX interfaces used below (mmap, PATH_MAX, ...) need the feature macro
// active before any system header is included.
#define _POSIX_C_SOURCE 200809L

#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


#define SECTOR_SIZE 512

struct jpeg_info
{
    uint64_t size;   // total JPEG length in bytes (SOI through EOI)
    char date[32];   // EXIF DateTimeOriginal ("YYYY:MM:DD HH:MM:SS")
};

static uint16_t read_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// exif points at the APP1 payload ("Exif\0\0" followed by the TIFF header),
// with `len` bytes available. Fills info->date from tag 0x9003 if present.
static void parse_exif(const uint8_t *exif, uint64_t len, struct jpeg_info *info)
{
    uint32_t ifd_off, n_entries;

    if (len < 14) return;
    if (memcmp(exif, "Exif\0\0", 6) != 0) return;
    if (exif[6] != 'I' || exif[7] != 'I') return;      // little-endian TIFF only
    if (read_le16(exif + 8) != 42) return;

    ifd_off = read_le32(exif + 10);
    if ((uint64_t)ifd_off + 2 > len - 6) return;       // IFD offsets are relative to "II"
    n_entries = read_le16(exif + 6 + ifd_off);
    if ((uint64_t)6 + ifd_off + 2 + (uint64_t)n_entries * 12 > len) return;

    for (uint32_t i = 0; i < n_entries; i++)
    {
        const uint8_t *e = exif + 6 + ifd_off + 2 + (uint64_t)i * 12;
        uint16_t tag = read_le16(e);
        uint16_t type = read_le16(e + 2);
        uint32_t count = read_le32(e + 4);
        uint64_t data_pos, data_size = count;

        if (type != 2 || count == 0) continue;           // ASCII strings only

        if (data_size <= 4)                              // short strings live inline
            data_pos = (uint64_t)(e - exif) + 8;
        else                                             // otherwise an IFD-relative offset
            data_pos = (uint64_t)6 + read_le32(e + 8);

        if (data_pos + data_size > len) continue;

        const char *s = (const char *)exif + data_pos;

        if (tag == 0x9003)                               // DateTimeOriginal
        {
            size_t n = data_size > 1 ? data_size - 1 : 0; // drop trailing NUL
            if (n >= sizeof(info->date)) n = sizeof(info->date) - 1;
            memcpy(info->date, s, n);
            info->date[n] = '\0';
        }
    }

}

// Scan entropy-coded data for the EOI marker (FF D9). FF 00 is a stuffed data
// byte and FF D0..D7 are restart markers; codecs never emit a bare FF D9
// before the real end of image, so the first one found is the EOI.
static uint64_t find_eoi(const uint8_t *img, uint64_t start, uint64_t limit)
{
    uint64_t pos = start;

    while (pos + 1 < limit)
    {
        if (img[pos] != 0xff) { pos++; continue; }

        while (pos < limit && img[pos] == 0xff) pos++;   // tolerate FF runs
        if (pos >= limit) return 0;

        uint8_t b = img[pos];
        if (b == 0x00 || (b >= 0xd0 && b <= 0xd7)) { pos++; continue; }
        if (b == 0xd9) return pos + 1;

        pos++;
    }

    return 0;
}

// Validate a candidate JPEG and compute its exact size.
// Detection:
//   1. SOI marker (FF D8)
//   2. a well-formed marker chain ending in EOI (FF D9); any EXIF APP1
//      segments encountered are parsed for DateTimeOriginal
// On success fills info->size / info->date and returns 1.
static int identify_jpeg(const uint8_t *img, uint64_t limit, struct jpeg_info *info)
{
    uint64_t pos = 2, size = 0;

    if (limit < 4 || img[0] != 0xff || img[1] != 0xd8) return 0;

    while (pos + 1 < limit)
    {
        if (img[pos] != 0xff) return 0;

        uint8_t m = img[pos + 1];
        if (m == 0xd9)                                  // EOI
        {
            size = pos + 2;
            break;
        }
        if (m == 0xd8) return 0;                        // stray SOI before EOI
        if (m == 0x01 || (m >= 0xd0 && m <= 0xd7)) { pos += 2; continue; }

        if (pos + 4 > limit) return 0;
        uint16_t seg_len = read_be16(img + pos + 2);
        if (seg_len < 2) return 0;

        if (m == 0xe1)                                  // APP1 / EXIF
        {
            if (pos + 2 + seg_len > limit) return 0;
            parse_exif(img + pos + 4, seg_len - 2, info);
        }

        if (m == 0xda)                                  // SOS: entropy data follows
        {
            uint64_t e = pos + 2 + seg_len;
            if (e > limit) return 0;
            size = find_eoi(img, e, limit);
            break;
        }

        pos += 2 + seg_len;
    }

    if (size == 0) return 0;
    info->size = size;
    return 1;
}

// Standard-C replacement for the glibc "%m" printf specifier: prints
// "<formatted message>: <strerror(errno)>" to stderr. Call it right after
// the failing syscall while errno is still set.
static void perrorf(const char *fmt, ...)
{
    int err = errno;
    char msg[PATH_MAX + 128]; // messages embed full paths
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    errno = err; // vsnprintf may have clobbered it
    perror(msg);
}

// Write all count bytes to fd, handling partial writes and EINTR.
// Returns 0 on success, or -1 on error with errno set.
static int write_all(int fd, const void *buf, uint64_t count)
{
    const uint8_t *p = (const uint8_t *)buf;

    while (count > 0)
    {
        size_t chunk = count > SSIZE_MAX ? SSIZE_MAX : (size_t)count;
        ssize_t written = write(fd, p, chunk);
        if (written < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (written == 0)
        {
            errno = ENOSPC;
            return -1;
        }

        p += written;
        count -= (uint64_t)written;
    }

    return 0;
}

int main(int argc, char **argv)
{
    struct stat img_stat;
    const char *img_file = NULL;
    void *img_data = NULL;
    uint64_t src_size = 0;
    int img_fd = -1;          // not open yet

    if (argc != 3)
    {
        fprintf(stderr, "Usage dslr_jpen_recovery <img_file> <dest_dir>\n");
        return EXIT_FAILURE;
    }

    setbuf(stdout, NULL); // disable buffering on stdout

    img_file = argv[1];

    // mmap file.
    img_fd = open(argv[1], O_RDONLY);
    if (img_fd == -1)
    {
        perrorf("Error opening image file(%s)", img_file);
        return EXIT_FAILURE;
    }

    if (fstat(img_fd, &img_stat) != 0)
    {
        perrorf("Error stating image file(%s)", img_file);
        close(img_fd);
        return EXIT_FAILURE;
    }

    img_data = mmap(NULL, img_stat.st_size, PROT_READ, MAP_PRIVATE, img_fd, 0);
    if (img_data == MAP_FAILED)
    {
        perrorf("Error mapping image file(%s)", img_file);
        close(img_fd);
        return EXIT_FAILURE;
    }

    // The mapping stays valid after the fd is closed (mmap holds a reference
    // to the open file description), so release the fd as soon as possible.
    close(img_fd);
    img_fd = -1;

    src_size = img_stat.st_size;

    // search for jpeg header on every sector start(512b)
    // `c + SECTOR_SIZE < src_size` avoids the uint64 underflow of the old
    // `c < src_size - 512` for files smaller than a sector, and guarantees the
    // whole sector is mapped so the header reads are always in bounds.
    for (uint64_t c = 0; c + SECTOR_SIZE < src_size; c += SECTOR_SIZE)
    {
        const uint8_t *ptr = (const uint8_t *)img_data + c;
        struct jpeg_info info = {0};

        if (identify_jpeg(ptr, src_size - c, &info))
        {
            if (info.date[0] != '\0')
                printf("Found jpeg at address 0x%" PRIx64 " - Date image created: %s\n", c, info.date);
            else
                printf("Found jpeg at address 0x%" PRIx64 "\n", c);

            // info.date is bounded to 31 chars and a real path cannot exceed
            // PATH_MAX on disk, so a fixed stack buffer is safe here.
            char dest_img_name[PATH_MAX + 64];

            int str_size;
            if (info.date[0] != '\0')
                str_size = snprintf(dest_img_name, sizeof(dest_img_name), "%s/IMG_%s.jpg", argv[2], info.date);
            else
                str_size = snprintf(dest_img_name, sizeof(dest_img_name), "%s/IMG_%" PRIx64 ".jpg", argv[2], c);
            if (str_size < 0)
            {
                fprintf(stderr, "Error formatting img file name\n");
                break;
            }
            if ((size_t)str_size >= sizeof(dest_img_name))
            {
                fprintf(stderr, "Destination path too long: %s\n", argv[2]);
                break;
            }

            int out_fd = open(dest_img_name, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
            if (out_fd == -1)
            {
                perrorf("Error creating img file(%s)", dest_img_name);
                break;
            }

            if (write_all(out_fd, ptr, info.size) < 0)
            {
                perrorf("Error writing img file(%s)", dest_img_name);
            }

            close(out_fd);
        }
    }

    munmap(img_data, src_size);

    return EXIT_SUCCESS;
}
