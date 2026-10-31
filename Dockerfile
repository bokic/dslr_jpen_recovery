# Build stage
FROM alpine:3.20 AS builder

RUN apk add --no-cache gcc musl-dev

WORKDIR /src
COPY main.c build.sh ./
RUN chmod +x build.sh && ./build.sh

# Runtime stage
FROM alpine:3.20

WORKDIR /data

COPY --from=builder /src/dslr_jpen_recovery /usr/local/bin/dslr_jpen_recovery

ENTRYPOINT ["dslr_jpen_recovery"]
