#!/bin/sh
# Build nikon-jpeg-recovery with GCC.
set -e

cc="${CC:-gcc}"

echo "Building with $cc..."
"$cc" -std=c11 -O2 -Wall -Wextra -o nikon-jpeg-recovery main.c

echo "Built ./nikon-jpeg-recovery"