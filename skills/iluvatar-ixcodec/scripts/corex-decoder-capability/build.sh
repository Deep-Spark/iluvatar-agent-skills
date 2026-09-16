#!/bin/bash
cd "$(dirname "$0")"

rm -rf build
cmake -S . -B build || exit 1
cmake --build build -j"$(nproc)" || exit 1
