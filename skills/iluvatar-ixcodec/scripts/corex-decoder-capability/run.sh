#!/bin/bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SKILL_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
VIDEO_DIR="${VIDEO_DIR:-$SKILL_DIR/assets/videos}"
VIDEO_GEN="$VIDEO_DIR/generate.sh"
cd "$SCRIPT_DIR"

if [[ "$#" -ne 0 ]]; then
    echo "usage: bash run.sh" >&2
    exit 2
fi
OUTDIR="${OUTDIR:-$VIDEO_DIR/outputs/decoder}"
mkdir -p "$OUTDIR"
exec > >(tee "$OUTDIR/run.log") 2>&1
export LD_LIBRARY_PATH=/usr/local/corex/lib64:${LD_LIBRARY_PATH:-}

nm -D --defined-only /usr/local/corex/lib64/libnvcuvid.so > "$OUTDIR/libnvcuvid.symbols.txt"
grep -q ' ixvidGetDecoderCaps@@' "$OUTDIR/libnvcuvid.symbols.txt"
grep -q ' ixvidCreateVideoParser@@' "$OUTDIR/libnvcuvid.symbols.txt"
! grep -q ' ixvidRefreshDecoder@@' "$OUTDIR/libnvcuvid.symbols.txt"

if [[ ! -x ./build/corex-decoder-capability.out ]]; then
    bash build.sh
fi

./build/corex-decoder-capability.out caps | tee "$OUTDIR/caps.log"
grep -q 'timestamp_fields source_packet=0 display_info=0 picture_params=1' "$OUTDIR/caps.log"
grep -q 'caps codec=MPEG4_PART2 chroma=420 bitdepth=8 .* supported=0 ' "$OUTDIR/caps.log"
grep -q 'caps codec=H264 chroma=420 bitdepth=8 .* supported=1 ' "$OUTDIR/caps.log"
grep -q 'caps codec=HEVC chroma=420 bitdepth=10 .* supported=1 ' "$OUTDIR/caps.log"
grep -q 'caps codec=H264 chroma=422 bitdepth=8 .* supported=1 ' "$OUTDIR/caps.log"
grep -q 'caps codec=AV1 chroma=420 bitdepth=8 .* supported=0 ' "$OUTDIR/caps.log"

H264_256="$VIDEO_DIR/h264_420_256x128.264"
H264_BASELINE_L52="$VIDEO_DIR/h264_baseline_l52_4096x2304.264"
H264_MAIN_L52="$VIDEO_DIR/h264_main_l52_4096x2304.264"
H264_HIGH_L52="$VIDEO_DIR/h264_high_l52_4096x2304.264"
H264_422_256="$VIDEO_DIR/h264_422_256x128.264"
H264_444_256="$VIDEO_DIR/h264_444_256x128.264"
HEVC_256="$VIDEO_DIR/hevc_420_256x128.265"
HEVC10_256="$VIDEO_DIR/hevc_420p10_256x128.265"
HEVC_MAIN_L51_HIGH="$VIDEO_DIR/hevc_main_l51_high_4096x2160.265"
HEVC_MAIN10_L51_HIGH="$VIDEO_DIR/hevc_main10_l51_high_4096x2160.265"
HEVC_422_256="$VIDEO_DIR/hevc_422_256x128.265"
HEVC_444_256="$VIDEO_DIR/hevc_444_256x128.265"
H264_1080="$VIDEO_DIR/h264_420_1920x1080.264"
VP9_256="$VIDEO_DIR/vp9_420_256x128.vp9"

bash "$VIDEO_GEN" h264 256 128 "$H264_256"
bash "$VIDEO_GEN" h264profile baseline 5.2 4096 2304 "$H264_BASELINE_L52"
bash "$VIDEO_GEN" h264profile main 5.2 4096 2304 "$H264_MAIN_L52"
bash "$VIDEO_GEN" h264profile high 5.2 4096 2304 "$H264_HIGH_L52"
bash "$VIDEO_GEN" h264fmt 256 128 yuv422p "$H264_422_256"
bash "$VIDEO_GEN" h264fmt 256 128 yuv444p "$H264_444_256"
bash "$VIDEO_GEN" hevc 256 128 yuv420p "$HEVC_256"
bash "$VIDEO_GEN" hevc 256 128 yuv420p10le "$HEVC10_256"
bash "$VIDEO_GEN" hevcprofile main 5.1 high 4096 2160 yuv420p "$HEVC_MAIN_L51_HIGH"
bash "$VIDEO_GEN" hevcprofile main10 5.1 high 4096 2160 yuv420p10le "$HEVC_MAIN10_L51_HIGH"
bash "$VIDEO_GEN" hevc 256 128 yuv422p "$HEVC_422_256"
bash "$VIDEO_GEN" hevc 256 128 yuv444p "$HEVC_444_256"
bash "$VIDEO_GEN" h264 1920 1080 "$H264_1080"
bash "$VIDEO_GEN" vp9 256 128 "$VP9_256"

direct_timestamp="$(./build/corex-decoder-capability.out bitstream \
    h264_420_256x128_nv12 0 0 0 1 0 "$H264_256" "$OUTDIR/h264_420_256x128.nv12")"
echo "$direct_timestamp"
grep -q 'map_timestamp=1' <<<"$direct_timestamp"
./build/corex-decoder-capability.out bitstream h264_420_256x128_i420 0 0 0 0 0 "$H264_256" "$OUTDIR/h264_420_256x128.i420"
./build/corex-decoder-capability.out bitstream h264_420_256x128_nv21 0 0 0 1 1 "$H264_256" "$OUTDIR/h264_420_256x128.nv21"
python3 "$SCRIPT_DIR/verify-layouts.py" 256 128 \
    "$OUTDIR/h264_420_256x128.i420" \
    "$OUTDIR/h264_420_256x128.nv12" \
    "$OUTDIR/h264_420_256x128.nv21"
./build/corex-decoder-capability.out bitstream h264_baseline_l52_4096x2304_nv12 0 0 0 1 0 "$H264_BASELINE_L52"
./build/corex-decoder-capability.out bitstream h264_main_l52_4096x2304_nv12 0 0 0 1 0 "$H264_MAIN_L52"
./build/corex-decoder-capability.out bitstream h264_high_l52_4096x2304_nv12 0 0 0 1 0 "$H264_HIGH_L52"
./build/corex-decoder-capability.out bitstream hevc_420_256x128_nv12 12 0 0 1 0 "$HEVC_256"
./build/corex-decoder-capability.out bitstream hevc_420p10_256x128_nv12 12 0 0 1 0 "$HEVC10_256"
./build/corex-decoder-capability.out bitstream hevc_main_l51_high_4096x2160_nv12 12 0 0 1 0 "$HEVC_MAIN_L51_HIGH"
./build/corex-decoder-capability.out bitstream hevc_main10_l51_high_4096x2160_nv12 12 0 0 1 0 "$HEVC_MAIN10_L51_HIGH"
run_geometry() {
    local name="$1"
    local scale_w="$2"
    local scale_h="$3"
    local expected_pitch="$4"
    local expected_height="$5"
    local output
    output="$(./build/corex-decoder-capability.out bitstream \
        "$name" 0 "$scale_w" "$scale_h" 1 0 "$H264_1080")"
    echo "$output"
    grep -q "result name=$name got=1 pitch=$expected_pitch height=$expected_height" <<<"$output"
}

run_geometry h264_420_1920x1080_scale_896x480 896 480 896 480
run_geometry h264_420_1920x1080_scale_854x480 854 480 896 480
run_geometry h264_420_1920x1080_scale_848x480 848 480 896 480
run_parser() {
    local name="$1"
    local codec="$2"
    local bit_format="$3"
    local bitstream="$4"
    local output
    output="$(./build/corex-decoder-capability.out parser \
        "$name" "$codec" "$bit_format" "$bitstream")"
    echo "$output"
    grep -q 'picture_decode=1 eos_decode=1' <<<"$output"
    grep -q 'map_timestamp=0' <<<"$output"
}

run_parser h264_parser 4 0 "$H264_256"
run_parser hevc_parser 8 12 "$HEVC_256"
run_parser vp9_parser 10 13 "$VP9_256"

./build/corex-decoder-capability.out bitstream vp9_420_256x128_nv12 13 0 0 1 0 "$VP9_256"

H264_8192="$VIDEO_DIR/h264_420_8192x8192.264"
HEVC_8192="$VIDEO_DIR/hevc_420_8192x8192.265"
bash "$VIDEO_GEN" h264 8192 8192 "$H264_8192"
bash "$VIDEO_GEN" hevc 8192 8192 yuv420p "$HEVC_8192"
./build/corex-decoder-capability.out bitstream h264_420_8192x8192_nv12 0 0 0 1 0 "$H264_8192"
./build/corex-decoder-capability.out bitstream hevc_420_8192x8192_nv12 12 0 0 1 0 "$HEVC_8192"

run_unsupported() {
    local name="$1"
    local bit_format="$2"
    local bitstream="$3"
    set +e
    timeout 20 ./build/corex-decoder-capability.out bitstream "$name" "$bit_format" 0 0 1 0 "$bitstream"
    local rc=$?
    set -e
    if [[ "$rc" -ne 20 ]]; then
        echo "UNEXPECTED_RESULT: $name rc=$rc (expected 20)"
        return 1
    fi
    echo "UNSUPPORTED: $name rc=$rc"
    ixsmi -i 0 -r || true
}

run_unsupported h264_422_256x128_nv12 0 "$H264_422_256"
run_unsupported h264_444_256x128_nv12 0 "$H264_444_256"
run_unsupported hevc_422_256x128_nv12 12 "$HEVC_422_256"
run_unsupported hevc_444_256x128_nv12 12 "$HEVC_444_256"

echo "PASS: decoder capability probe finished. videos: $VIDEO_DIR outputs: $OUTDIR"
