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
OUTDIR="${OUTDIR:-$VIDEO_DIR/outputs/encoder}"
mkdir -p "$OUTDIR"
export LD_LIBRARY_PATH=/usr/local/corex/lib64:${LD_LIBRARY_PATH:-}

if [[ ! -x ./build/corex-encoder-capability.out ]]; then
    bash build.sh
fi

probe_out() {
    local out="$1"
    local expected="$2"
    local actual
    actual="$(ffprobe -hide_banner -v error -select_streams v:0 \
        -show_entries stream=codec_name,profile,width,height,pix_fmt,bits_per_raw_sample \
        -of csv=p=0 "$out")"
    echo "$actual"
    [[ "$actual" == "$expected" ]]
    ffmpeg -hide_banner -loglevel error -y -i "$out" -frames:v 1 "${out}.jpg"
    test -s "${out}.jpg"
}

run_case() {
    local name="$1"
    local rawfmt="$2"
    local w="$3"
    local h="$4"
    local expected="$5"
    local raw="$VIDEO_DIR/${name}.${rawfmt}.raw"
    local out="$OUTDIR/${name}.bitstream"
    bash "$VIDEO_GEN" raw "$rawfmt" "$w" "$h" "$raw"
    ./build/corex-encoder-capability.out "$name" "$raw" "$out"
    probe_out "$out" "$expected"
}

run_case h264_420_8bit_256x128 nv12 256 128 'h264,Baseline,256,128,yuv420p,8'
run_case h264_profile0_256x128 nv12 256 128 'h264,High,256,128,yuv420p,8'
run_case h264_profile1_256x128 nv12 256 128 'h264,Baseline,256,128,yuv420p,8'
run_case h264_profile2_256x128 nv12 256 128 'h264,Main,256,128,yuv420p,8'
run_case h264_profile3_256x128 nv12 256 128 'h264,Extended,256,128,yuv420p,8'
run_case hevc_420_8bit_256x128 nv12 256 128 'hevc,Main,256,128,yuv420p,N/A'
run_case hevc_420_p10_main10_256x128 p010le 256 128 'hevc,Main 10,256,128,yuv420p10le,N/A'
run_case hevc_nv16_422_input_256x128 nv16 256 128 'hevc,Main,256,128,yuv420p,N/A'
run_case hevc_p210_422_input_main10_256x128 p210le 256 128 'hevc,Main 10,256,128,yuv420p10le,N/A'
run_case h264_420_8bit_264x128 nv12 264 128 'h264,Baseline,264,128,yuv420p,8'
run_case h264_420_8bit_256x136 nv12 256 136 'h264,Baseline,256,136,yuv420p,8'
run_case hevc_420_8bit_264x128 nv12 264 128 'hevc,Main,264,128,yuv420p,N/A'
run_case hevc_420_8bit_256x136 nv12 256 136 'hevc,Main,256,136,yuv420p,N/A'

run_case h264_420_8bit_8192x8192 nv12 8192 8192 'h264,Baseline,8192,8192,yuv420p,8'
run_case hevc_420_8bit_8192x8192 nv12 8192 8192 'hevc,Main,8192,8192,yuv420p,N/A'

echo "PASS: encoder capability probe finished. outputs: $OUTDIR"
