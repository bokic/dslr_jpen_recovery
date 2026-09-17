#!/bin/sh
# Build nikon-jpeg-recovery with the first available compiler:
# prefer the $CC environment variable, then gcc, then clang.
set -e

cc=""

if [ -n "${CC:-}" ] && command -v "$CC" >/dev/null 2>&1; then
    cc="$CC"
elif command -v gcc >/dev/null 2>&1; then
    cc=gcc
elif command -v clang >/dev/null 2>&1; then
    cc=clang
fi

if [ -z "$cc" ]; then
    echo "No compilers detected" >&2
    exit 1
fi

echo "Building with $cc..."
"$cc" -std=c11 -O2 -Wall -Wextra -o nikon-jpeg-recovery main.c

echo "Built ./nikon-jpeg-recovery"