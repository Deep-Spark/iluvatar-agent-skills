#!/bin/bash
cd "$(dirname "$0")"

export CUDA_HOME=/usr/local/corex
export TensorRT_ROOT=$CUDA_HOME
export CMAKE_CUDA_ARCHITECTURES=ivcore11
export LIBRARY_PATH=$CUDA_HOME/lib64

rm -rf build
cmake -S . -B build \
  -DTensorRT_INCLUDE_DIR="$TensorRT_ROOT/include" \
  -DTensorRT_LIBRARY=ixrt \
  -DTensorRT_LIBRARIES="ixrt;ixrt_plugin" \
  -DTensorRT_Plugin_INCLUDE_DIR="$TensorRT_ROOT/include" \
  -DTensorRT_Plugin_LIBRARY=ixrt_plugin \
  2>&1 | tail -10
ec=${PIPESTATUS[0]}
[ "$ec" -ne 0 ] && { echo "cmake configure failed"; exit 1; }

cmake --build build -j"$(nproc)" 2>&1 | tail -8
ec=${PIPESTATUS[0]}
[ "$ec" -ne 0 ] && { echo "cmake build failed"; exit 1; }

./build/sample_non_zero_plugin || exit 1
