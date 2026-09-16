#!/bin/bash
# 原生 NVIDIA + nvjpeg 环境构建(同一份 CMakeLists；不设 CMAKE_CUDA_ARCHITECTURES，用 NV 默认)。
# nvjpegEncodeYUV 是 NV 标准 API；Corex 私有的 IxjpegEncoderParamsSetFrameFormat / NV12·NV21
# 半平面组合已用 __ILUVATAR__ 守卫，NV 上自动跳过，只跑 planar(420/422/440/444/GRAY)。
cd "$(dirname "$0")"
export CMAKE_CUDA_ARCHITECTURES=native

rm -rf build
cmake -S . -B build || exit 1
cmake --build build -j"$(nproc)" || exit 1

./build/ixjpeg-encode-yuv.out
