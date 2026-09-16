#!/bin/bash
cd "$(dirname "$0")"
export CMAKE_CUDA_ARCHITECTURES=ivcore11

assets="../../assets/images"

rm -rf build
cmake -S . -B build || exit 1
cmake --build build -j"$(nproc)" || exit 1

./build/ixjpeg-decode-check.out "$assets"
