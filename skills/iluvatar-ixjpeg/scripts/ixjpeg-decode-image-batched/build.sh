#!/bin/bash
cd "$(dirname "$0")"
export CMAKE_CUDA_ARCHITECTURES=ivcore11

rm -rf build
cmake -S . -B build || exit 1
cmake --build build -j"$(nproc)" || exit 1

mkdir -p build/baseline-inputs
cp ../../assets/images/baseline_420_even.jpg build/baseline-inputs/
cp ../../assets/images/baseline_422_even.jpg build/baseline-inputs/
cp ../../assets/images/baseline_444_even.jpg build/baseline-inputs/
cp ../../assets/images/cat_baseline.jpg build/baseline-inputs/

for fmt in rgb bgr rgbi bgri yuv unchanged y nv12 nv21; do
  echo "===== fmt=$fmt ====="
  ./build/ixjpeg-decode-image-batched.out \
    -i build/baseline-inputs/ -b 4 -t 4 -fmt "$fmt" || exit 1
done
