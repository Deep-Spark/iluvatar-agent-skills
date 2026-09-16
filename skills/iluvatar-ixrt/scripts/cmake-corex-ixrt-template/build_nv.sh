#!/bin/bash
cd "$(dirname "$0")"
export CMAKE_CUDA_ARCHITECTURES=native

rm -rf build
cmake -S . -B build \
  -DTensorRT_INCLUDE_DIR=/usr/include/x86_64-linux-gnu \
  -DTensorRT_LIBRARY=nvinfer \
  -DTensorRT_LIBRARIES="nvinfer;nvinfer_plugin" \
  -DTensorRT_Plugin_INCLUDE_DIR=/usr/include/x86_64-linux-gnu \
  -DTensorRT_Plugin_LIBRARY=nvinfer_plugin \
  2>&1 | tail -10
ec=${PIPESTATUS[0]}
[ "$ec" -ne 0 ] && { echo "cmake configure failed"; exit 1; }

cmake --build build -j"$(nproc)" 2>&1 | tail -8
ec=${PIPESTATUS[0]}
[ "$ec" -ne 0 ] && { echo "cmake build failed"; exit 1; }

./build/sample_non_zero_plugin || exit 1
