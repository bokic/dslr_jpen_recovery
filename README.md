### Features
Recover JPEG photos from an SD card, regardless of camera make. The input can be
either a raw disk-image file or the path to the physical SD card device itself (e.g.
`/dev/sdb` or `/dev/mmcblk0`).

The tool scans the input at every 512-byte sector boundary and structurally validates each
candidate: it walks the JPEG marker chain from the SOI marker (`FF D8`) to the EOI marker
(`FF D9`) so the exact file size is known, and reads the EXIF capture date
(**DateTimeOriginal**, tag `0x9003`) when available.

The tool recovers well-formed JPEGs regardless of source camera. EXIF capture dates are
used in recovered filenames when available.

Recovered photos are written to `<dest_dir>` as:
- `IMG_<date>.jpg` — when an EXIF capture date is available (e.g. `IMG_2015:11:21 12:24:55.jpg`)
- `IMG_<offset>.jpg` — when the JPEG has no EXIF date (e.g. `IMG_0.jpg`)

### How to build
Requires a C11 compiler and POSIX APIs (`mmap`, ...). Linux/macOS:

```shell
$ ./build.sh
```

`build.sh` uses the first compiler it finds: `$CC` if set, then `gcc`, then
`clang`. If none are installed it prints *No compilers detected*.

produces `./dslr_jpen_recovery`.

### Run
Both a disk image and a physical SD card device work — the tool mmaps the input, so any
path that can be opened for reading is valid:

```shell
$ ./dslr_jpen_recovery ../sdcard.img .
Found jpeg at address 0x820000 - Date image created: 2015:11:21 12:24:55
Found jpeg at address 0xf08000 - Date image created: 2015:11:21 12:40:36
Found jpeg at address 0x1580000 - Date image created: 2015:11:21 12:41:01
Found jpeg at address 0x1bc0000 - Date image created: 2015:11:21 12:41:13
Found jpeg at address 0x2218000 - Date image created: 2015:11:21 12:42:28
...
```

```shell
$ ./dslr_jpen_recovery /dev/sdb recover/
```

Reading a device node requires sufficient permissions (`root`, or a user in the `disk`
group). Unmount the card first — the tool only reads, but a mounted card can change
underneath it, and writing the recovered JPEGs to a directory on that same card is not
recommended.

Usage: `dslr_jpen_recovery <img_file> <dest_dir>`

The `<img_file>` argument is the disk image or SD card device path.
