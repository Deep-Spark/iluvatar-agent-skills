#!/bin/bash
cd "$(dirname "$0")"
export CMAKE_CUDA_ARCHITECTURES=native

rm -rf build
cmake -S . -B build || exit 1
cmake --build build -j"$(nproc)" || exit 1

mkdir -p build/baseline-inputs
cp ../../assets/images/baseline_420_even.jpg build/baseline-inputs/
cp ../../assets/images/baseline_422_even.jpg build/baseline-inputs/
cp ../../assets/images/baseline_444_even.jpg build/baseline-inputs/
cp ../../assets/images/cat_baseline.jpg build/baseline-inputs/

./build/ixjpeg-decode-image-batched.out -i build/baseline-inputs/ -b 4 -t 4
