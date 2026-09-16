#!/bin/bash
cd "$(dirname "$0")"
export CMAKE_CUDA_ARCHITECTURES=native

rm -rf build
cmake -S . -B build || exit 1
cmake --build build -j"$(nproc)" || exit 1

./build/ixjpeg-decode-image.out -i ../../assets/images/img1.jpg
