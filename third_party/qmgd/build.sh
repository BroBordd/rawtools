#!/bin/sh
# Quick build without CMake. For a proper out-of-tree build, use CMake instead:
#   cmake -B build && cmake --build build
set -e
gcc -O2 -Wall -Iinclude -Isrc -o qmgd_dump \
    src/header.c src/decode_w2pass.c src/decode_a9ll.c src/decode.c \
    src/tools/qmgd_dump.c -lm
echo "built ./qmgd_dump"
