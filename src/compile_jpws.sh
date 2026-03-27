#!/bin/bash

# compile_jpws.sh

g++ -std=c++23 -O3 -march=native -pipe \
    -Wall -Wextra -Wpedantic -Wformat=2 -Wformat-security -Wconversion -Wsign-conversion \
    -fstack-protector-strong -D_FORTIFY_SOURCE=2 -D_GLIBCXX_ASSERTIONS \
    -fPIE -DNDEBUG -s -flto=auto -fuse-linker-plugin \
    args.cpp file_utils.cpp jpeg_process.cpp jpws.cpp \
    -lturbojpeg -pie -Wl,-z,relro,-z,now -o jpws

if [ $? -eq 0 ]; then
    echo "Compilation successful. Executable 'jpws' created."
else
    echo "Compilation failed."
    exit 1
fi
