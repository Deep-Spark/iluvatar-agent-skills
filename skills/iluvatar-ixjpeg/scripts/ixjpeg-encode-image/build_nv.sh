#!/bin/bash
# 原生 NVIDIA + nvjpeg 环境构建(同一份 CMakeLists；不设 CMAKE_CUDA_ARCHITECTURES，用 NV 默认)。
# nvjpegEncodeImage 是 NV 标准 API，RGB/BGR/RGBI/BGRI 两平台通用。
cd "$(dirname "$0")"

rm -rf build
cmake -S . -B build || exit 1
cmake --build build -j"$(nproc)" || exit 1

./build/ixjpeg-encode-image.out
