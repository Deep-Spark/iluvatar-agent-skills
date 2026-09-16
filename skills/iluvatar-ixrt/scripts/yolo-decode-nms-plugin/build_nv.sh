#!/bin/bash
cd "$(dirname "$0")"

rm -rf build
cmake -S . -B build \
  -DCMAKE_CUDA_ARCHITECTURES=native \
  -DTensorRT_INCLUDE_DIR=/usr/include/x86_64-linux-gnu \
  -DTensorRT_LIBRARY_DIR=/usr/lib/x86_64-linux-gnu \
  -DTensorRT_Plugin_LIBRARY=nvinfer_plugin \
  -DTensorRT_LIBRARIES=nvinfer || exit 1
cmake --build build -j"$(nproc)" || exit 1

cp -f build/libyolo_decode_nms.so ./libyolo_decode_nms.so || exit 1
echo "built: libyolo_decode_nms.so"
