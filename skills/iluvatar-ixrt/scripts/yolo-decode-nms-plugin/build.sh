#!/bin/bash
cd "$(dirname "$0")"
export CUDA_HOME=/usr/local/corex
export TensorRT_ROOT=$CUDA_HOME
export CMAKE_CUDA_ARCHITECTURES=ivcore11
export LIBRARY_PATH=$CUDA_HOME/lib64

rm -rf build
cmake -S . -B build \
  -DTensorRT_INCLUDE_DIR=$TensorRT_ROOT/include \
  -DTensorRT_LIBRARIES=ixrt \
  -DTensorRT_Plugin_LIBRARY=ixrt_plugin \
  -DTensorRT_LIBRARY_DIR=$TensorRT_ROOT/lib64 || exit 1
cmake --build build -j"$(nproc)" || exit 1

cp -f build/libyolo_decode_nms.so ./libyolo_decode_nms.so || exit 1
echo "built: libyolo_decode_nms.so"
