#!/bin/bash

# compile_jpws.sh

set -euo pipefail

compile_flags=(
    -std=c++23 -O3 -pipe
    -Wall -Wextra -Wpedantic -Wformat=2 -Wformat-security -Wconversion -Wsign-conversion
    -fstack-protector-strong -fstack-clash-protection -fcf-protection=full
    -D_FORTIFY_SOURCE=3 -D_GLIBCXX_ASSERTIONS
    -fPIE -DNDEBUG -s -flto=auto -fuse-linker-plugin
)

if [[ "${JPWS_NATIVE:-0}" == "1" ]]; then
    compile_flags+=(-march=native)

    native_macros=$(g++ -march=native -dM -E -x c++ /dev/null)
    if [[ "$native_macros" == *"#define __FMA__"* ]]; then
        compile_flags+=(-DSTBIR_USE_FMA)
    fi
fi

g++ "${compile_flags[@]}" \
    args.cpp file_utils.cpp jpeg_process.cpp jpws.cpp \
    -lturbojpeg -pie -Wl,-z,relro,-z,now,-z,noexecstack,-z,separate-code -o jpws

echo "Compilation successful. Executable 'jpws' created."
