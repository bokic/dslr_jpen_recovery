#!/bin/sh
# Build nikon-jpeg-recovery with Clang.
set -e

cc="${CC:-clang}"

echo "Building with $cc..."
"$cc" -std=c11 -O2 -Wall -Wextra -o nikon-jpeg-recovery main.c

echo "Built ./nikon-jpeg-recovery"