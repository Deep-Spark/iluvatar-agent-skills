#!/usr/bin/env bash
set -uo pipefail
cd "$(dirname "$0")" || exit 1
export CMAKE_CUDA_ARCHITECTURES=native
rm -rf build-nv || exit 1
cmake -S . -B build-nv || exit 1
cmake --build build-nv -j"$(nproc)" || exit 1
./build-nv/cmake-findixrt-migration || exit 1
