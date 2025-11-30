#!/bin/bash

# compile_jpws.sh

g++ -std=c++23 -O3 -march=native -pipe -Wall -Wextra -Wpedantic -DNDEBUG -s -flto=auto -fuse-linker-plugin jpws.cpp -lturbojpeg -o jpws

if [ $? -eq 0 ]; then
    echo "Compilation successful. Executable 'jpws' created."
else
    echo "Compilation failed."
    exit 1
fi
