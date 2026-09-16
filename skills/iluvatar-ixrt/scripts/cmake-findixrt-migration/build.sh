#!/usr/bin/env bash
set -uo pipefail
cd "$(dirname "$0")" || exit 1
export CMAKE_CUDA_ARCHITECTURES=ivcore11
rm -rf build || exit 1
cmake -S . -B build || exit 1
cmake --build build -j"$(nproc)" || exit 1
./build/cmake-findixrt-migration || exit 1
