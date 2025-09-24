#!/bin/bash

# compile_jpws.sh

g++ -std=c++20 jpws.cpp -Wall -O3 -lturbojpeg -s -o jpws

if [ $? -eq 0 ]; then
    echo "Compilation successful. Executable 'jpws' created."
else
    echo "Compilation failed."
    exit 1
fi
