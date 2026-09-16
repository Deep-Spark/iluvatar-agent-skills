#!/bin/bash
set -euo pipefail

usage() {
    cat >&2 <<'EOF'
usage:
  generate.sh raw <pix_fmt> <width> <height> <out.raw>
  generate.sh h264 <width> <height> <out.264>
  generate.sh h264fmt <width> <height> <pix_fmt> <out.264>
  generate.sh h264profile <profile> <level> <width> <height> <out.264>
  generate.sh hevc <width> <height> <pix_fmt> <out.265>
  generate.sh hevcprofile <profile> <level> <tier> <width> <height> <pix_fmt> <out.265>
  generate.sh vp9 <width> <height> <out.vp9>
EOF
}

if [[ $# -lt 1 ]]; then
    usage
    exit 1
fi

kind="$1"
shift

make_parent() {
    mkdir -p "$(dirname "$1")"
}

case "$kind" in
    raw)
        if [[ $# -ne 4 ]]; then
            usage
            exit 1
        fi
        pix_fmt="$1"
        width="$2"
        height="$3"
        out="$4"
        make_parent "$out"
        if [[ -s "$out" ]]; then
            exit 0
        fi
        ffmpeg -hide_banner -loglevel error -y \
            -f lavfi -i "testsrc2=size=${width}x${height}:rate=1" \
            -frames:v 1 -pix_fmt "$pix_fmt" -f rawvideo "$out"
        ;;
    h264)
        if [[ $# -ne 3 ]]; then
            usage
            exit 1
        fi
        width="$1"
        height="$2"
        out="$3"
        make_parent "$out"
        if [[ -s "$out" ]]; then
            exit 0
        fi
        ffmpeg -hide_banner -loglevel error -y \
            -f lavfi -i "testsrc2=size=${width}x${height}:rate=1" \
            -frames:v 1 -pix_fmt yuv420p \
            -c:v libx264 -preset ultrafast -tune zerolatency \
            -x264-params keyint=1:min-keyint=1:scenecut=0 \
            -f h264 "$out"
        ;;
    h264fmt)
        if [[ $# -ne 4 ]]; then
            usage
            exit 1
        fi
        width="$1"
        height="$2"
        pix_fmt="$3"
        out="$4"
        make_parent "$out"
        if [[ -s "$out" ]]; then
            exit 0
        fi
        ffmpeg -hide_banner -loglevel error -y \
            -f lavfi -i "testsrc2=size=${width}x${height}:rate=1" \
            -frames:v 1 -pix_fmt "$pix_fmt" \
            -c:v libx264 -preset ultrafast -tune zerolatency \
            -x264-params keyint=1:min-keyint=1:scenecut=0 \
            -f h264 "$out"
        ;;
    h264profile)
        if [[ $# -ne 5 ]]; then
            usage
            exit 1
        fi
        profile="$1"
        level="$2"
        width="$3"
        height="$4"
        out="$5"
        make_parent "$out"
        if [[ -s "$out" ]]; then
            exit 0
        fi
        case "$profile" in
            baseline|constrained_baseline)
                x264_profile="baseline"
                x264_tools="cabac=0:8x8dct=0"
                ;;
            main)
                x264_profile="main"
                x264_tools="cabac=1:8x8dct=0"
                ;;
            high)
                x264_profile="high"
                x264_tools="cabac=1:8x8dct=1"
                ;;
            *)
                echo "unsupported h264 profile: $profile" >&2
                exit 1
                ;;
        esac
        ffmpeg -hide_banner -loglevel error -y \
            -f lavfi -i "testsrc2=size=${width}x${height}:rate=1" \
            -frames:v 1 -pix_fmt yuv420p \
            -c:v libx264 -profile:v "$x264_profile" -level:v "$level" \
            -preset medium -tune zerolatency \
            -x264-params "keyint=1:min-keyint=1:scenecut=0:${x264_tools}" \
            -f h264 "$out"
        ;;
    hevc)
        if [[ $# -ne 4 ]]; then
            usage
            exit 1
        fi
        width="$1"
        height="$2"
        pix_fmt="$3"
        out="$4"
        make_parent "$out"
        if [[ -s "$out" ]]; then
            exit 0
        fi
        ffmpeg -hide_banner -loglevel error -y \
            -f lavfi -i "testsrc2=size=${width}x${height}:rate=1" \
            -frames:v 1 -pix_fmt "$pix_fmt" \
            -c:v libx265 -preset ultrafast \
            -x265-params keyint=1:min-keyint=1:scenecut=0 \
            -f hevc "$out"
        ;;
    hevcprofile)
        if [[ $# -ne 7 ]]; then
            usage
            exit 1
        fi
        profile="$1"
        level="$2"
        tier="$3"
        width="$4"
        height="$5"
        pix_fmt="$6"
        out="$7"
        make_parent "$out"
        if [[ -s "$out" ]]; then
            exit 0
        fi
        case "$tier" in
            high|High|HIGH) high_tier=1 ;;
            main|Main|MAIN) high_tier=0 ;;
            *)
                echo "unsupported hevc tier: $tier" >&2
                exit 1
                ;;
        esac
        ffmpeg -hide_banner -loglevel error -y \
            -f lavfi -i "testsrc2=size=${width}x${height}:rate=1" \
            -frames:v 1 -pix_fmt "$pix_fmt" \
            -c:v libx265 -profile:v "$profile" -preset ultrafast \
            -x265-params "level-idc=${level}:high-tier=${high_tier}" \
            -f hevc "$out"
        ;;
    vp9)
        if [[ $# -ne 3 ]]; then
            usage
            exit 1
        fi
        width="$1"
        height="$2"
        out="$3"
        make_parent "$out"
        if [[ -s "$out" ]]; then
            exit 0
        fi
        tmp="${out}.ivf.tmp"
        ffmpeg -hide_banner -loglevel error -y \
            -f lavfi -i "testsrc2=size=${width}x${height}:rate=1" \
            -frames:v 1 -pix_fmt yuv420p \
            -c:v libvpx-vp9 -lossless 1 -f ivf "$tmp"
        python3 - "$tmp" "$out" <<'PY'
import struct
import sys

data = open(sys.argv[1], "rb").read()
if len(data) < 44 or data[:4] != b"DKIF":
    raise SystemExit("invalid IVF output")
size = struct.unpack_from("<I", data, 32)[0]
frame = data[44:44 + size]
if len(frame) != size:
    raise SystemExit("truncated IVF frame")
open(sys.argv[2], "wb").write(frame)
PY
        rm -f "$tmp"
        ;;
    *)
        usage
        exit 1
        ;;
esac
