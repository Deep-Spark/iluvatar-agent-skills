#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SKILL_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUTDIR="${OUTDIR:-$SKILL_DIR/assets/videos/outputs/encoder_features}"
mkdir -p "$OUTDIR"
cd "$SCRIPT_DIR"

if [[ "$#" -ne 0 ]]; then
    echo "usage: bash run.sh" >&2
    exit 2
fi

export LD_LIBRARY_PATH=/usr/local/corex/lib64:${LD_LIBRARY_PATH:-}
IXENC_HEADER=/usr/local/corex/include/IX/ixcodec/ixEncodeAPI.h

if [[ ! -x ./build/corex-encoder-feature-probe.out ]]; then
    bash build.sh
fi

run_case() {
    local name="$1"
    local ext="$2"
    echo "=== RUN $name ==="
    ./build/corex-encoder-feature-probe.out "$name" "$OUTDIR/$name.$ext"
    assert_decodes_clean "$name" "$OUTDIR/$name.$ext"
}

assert_decodes_clean() {
    local name="$1"
    local file="$2"
    local log="$OUTDIR/$name.decode.log"
    ffmpeg -hide_banner -v error -i "$file" -f null - 2>"$log"
    if [[ -s "$log" ]]; then
        cat "$log"
        return 1
    fi
    echo "$name full_decode: PASS"
}

frame_types() {
    local file="$1"
    ffprobe -hide_banner -v error -select_streams v:0 \
        -show_entries frame=pict_type \
        -of csv=p=0 "$file" | sed '/^$/d' | paste -sd ' ' -
}

assert_frame_types() {
    local name="$1"
    local file="$2"
    local expected="$3"
    local types
    types="$(frame_types "$file")"
    echo "$name frame_types: $types"
    [[ "$types" == "$expected" ]]
}

max_num_ref_frames() {
    local file="$1"
    { ffmpeg -hide_banner -v verbose -i "$file" -c copy -bsf:v trace_headers -f null - 2>&1 || true; } \
        | awk '/max_num_ref_frames/ {print $NF; exit}'
}

assert_ref_frames() {
    local name="$1"
    local file="$2"
    local expected="$3"
    local got
    got="$(max_num_ref_frames "$file")"
    echo "$name max_num_ref_frames: $got"
    [[ "$got" == "$expected" ]]
}

write_lossless_expected() {
    python3 - "$OUTDIR/hevc_lossless_expected.nv12" <<'PY'
import sys
w, h, idx = 256, 128, 0
out = sys.argv[1]
buf = bytearray(w * h + w * h // 2)
for y in range(h):
    for x in range(w):
        buf[y * w + x] = (x * 3 + y * 5 + idx * 17) & 0xff
uv = w * h
for y in range(h // 2):
    for x in range(0, w, 2):
        buf[uv + y * w + x] = (64 + x + idx * 11) & 0xff
        buf[uv + y * w + x + 1] = (192 + y + idx * 7) & 0xff
open(out, "wb").write(buf)
PY
}

assert_hevc_lossless() {
    write_lossless_expected
    ffmpeg -hide_banner -loglevel error -y \
        -i "$OUTDIR/hevc_lossless_all_i.h265" \
        -frames:v 1 -pix_fmt nv12 -f rawvideo \
        "$OUTDIR/hevc_lossless_decoded.nv12"
    cmp -s "$OUTDIR/hevc_lossless_expected.nv12" "$OUTDIR/hevc_lossless_decoded.nv12"
    echo "hevc_lossless_cmp: PASS"
}

assert_no_lookahead_api() {
    ! grep -nEi 'look.?ahead|rc.?lookahead' "$IXENC_HEADER"
    ! nm -D /usr/local/corex/lib64/libnvencode.so 2>/dev/null | grep -Ei 'Look|Ahead'
    echo "lookahead_api_absent: PASS"
}

assert_no_quality_speed_preset_api() {
    sed '/\/\*/,/\*\//d; s,//.*$,,' "$IXENC_HEADER" \
        > "$OUTDIR/ixEncodeAPI.no_comments.h"
    ! grep -nEi 'presetGUID|presetGuid|tuningInfo|tuningPreset|NV_ENC_PRESET|NV_ENC_TUNING' \
        "$OUTDIR/ixEncodeAPI.no_comments.h"
    echo "quality_speed_preset_api_absent: PASS"
}

assert_no_alpha_av1_api() {
    ! grep -nEi 'alpha|rgba|bgra|argb|ayuv|yuva|av1' "$IXENC_HEADER"
    ! nm -D /usr/local/corex/lib64/libnvencode.so 2>/dev/null \
        | grep -Ei 'alpha|rgba|bgra|argb|ayuv|yuva|av1'
    echo "alpha_av1_api_absent: PASS"
}

assert_no_444_source_format() {
    sed -n '/IX_ENC_FORMAT_ERR/,/IX_ENC_SRC_FORMAT/p' "$IXENC_HEADER" \
        > "$OUTDIR/ixEncSrcFormat.h"
    ! grep -nEi '444|yuv444' "$OUTDIR/ixEncSrcFormat.h"
    grep -q 'BUFFER_FORMAT_444' "$IXENC_HEADER"
    echo "yuv444_source_format_absent: PASS"
}

h264_frame_mbs_only_flag() {
    local file="$1"
    { ffmpeg -hide_banner -v verbose -i "$file" -c copy -bsf:v trace_headers -f null - 2>&1 || true; } \
        | awk '/frame_mbs_only_flag/ {print $NF; exit}'
}

assert_h264_progressive_only() {
    local name="$1"
    local file="$2"
    local frame_mbs_only
    local field_order
    frame_mbs_only="$(h264_frame_mbs_only_flag "$file")"
    field_order="$(ffprobe -hide_banner -v error -select_streams v:0 \
        -show_entries stream=field_order -of default=nk=1:nw=1 "$file")"
    echo "$name frame_mbs_only_flag: $frame_mbs_only"
    echo "$name field_order: $field_order"
    [[ "$frame_mbs_only" == "1" ]]
    [[ "$field_order" == "progressive" ]]
}

assert_no_h264_interlaced_api() {
    ! grep -nEi 'interlac|mbaff|paff|pic_struct|picture.?struct|field_pic|bottom_field|top_field|frame_mbs_only|field.?order' \
        "$IXENC_HEADER"
    ! nm -D /usr/local/corex/lib64/libnvencode.so 2>/dev/null \
        | grep -Ei 'interlac|mbaff|paff|pic_struct|picture.?struct|field_pic|bottom_field|top_field|field.?order'
    echo "h264_interlaced_api_absent: PASS"
}

assert_reconfigure_marked_unsupported() {
    grep -B 10 -A 2 'IxEncReconfigureEncoder' "$IXENC_HEADER" \
        | grep -q 'Not support yet'
    echo "reconfigure_marked_unsupported: PASS"
}

assert_no_sequence_parameter_api() {
    ! grep -nEi 'IxEnc[^ (]*(Sequence|Sps|Pps|Vps)' "$IXENC_HEADER"
    echo "sequence_parameter_api_absent: PASS"
}

assert_hierarchical_temporal_ids() {
    local codec="$1"
    local file="$2"
    local result
    result="$(python3 "$SCRIPT_DIR/parse-temporal-ids.py" "$codec" "$file")"
    echo "$codec hierarchical: $result"
    grep -q '"temporal_ids": \[0, 1, 2\]' <<<"$result"
}

assert_hevc_single_temporal_id() {
    local name="$1"
    local file="$2"
    local ids
    ids="$({ ffmpeg -hide_banner -v verbose -i "$file" -c copy -bsf:v trace_headers -f null - 2>&1 || true; } \
        | awk '/nuh_temporal_id_plus1/ {print $NF}' | sort -u | tr '\n' ' ')"
    echo "$name nuh_temporal_id_plus1_values: $ids"
    grep -qw 1 <<<"$ids"
    ! grep -Eq '(^| )([2-9]|[1-9][0-9]+)( |$)' <<<"$ids"
}

run_empty_case() {
    local name="$1"
    echo "=== RUN $name ==="
    ./build/corex-encoder-feature-probe.out "$name" "$OUTDIR/$name.bitstream"
    test ! -s "$OUTDIR/$name.bitstream"
    ! ffprobe -hide_banner -v error "$OUTDIR/$name.bitstream"
}

api_surface="$(./build/corex-encoder-feature-probe.out api_surface "$OUTDIR/api_surface.unused")"
echo "$api_surface"
grep -q 'lossless_fields avc=0 hevc=1' <<<"$api_surface"

run_case h264_ipp h264
run_case h264_ipp_single h264
run_case h264_ibbb h264
run_case h264_ibpbp h264
run_case h264_ibbbp h264
run_case h264_ibbbb h264
run_case h264_ra_ib h264
run_case h264_custom_multiref h264
run_case h264_custom_hierarchical h264
run_case hevc_ipp h265
run_case hevc_ipp_single h265
run_case hevc_ibbb h265
run_case hevc_ra_ib h265
run_case hevc_custom_hierarchical h265
run_case hevc_max_merge_1 h265
run_case hevc_max_merge_2 h265
run_case hevc_strong_intra_smoothing_0 h265
run_case hevc_strong_intra_smoothing_1 h265
run_case hevc_lossless_all_i h265
run_empty_case unsupported_bitformat_13
run_empty_case unsupported_bitformat_14
run_empty_case unsupported_bitformat_15

assert_frame_types h264_ibbb "$OUTDIR/h264_ibbb.h264" 'I B B B B B B B'
assert_frame_types h264_ibpbp "$OUTDIR/h264_ibpbp.h264" 'I B P B P B P P'
assert_frame_types h264_ibbbp "$OUTDIR/h264_ibbbp.h264" 'I B B B P P B B'
assert_frame_types h264_ibbbb "$OUTDIR/h264_ibbbb.h264" 'I B B B B B B B'
assert_frame_types h264_ra_ib "$OUTDIR/h264_ra_ib.h264" 'I B B B B B B B'
assert_frame_types hevc_ibbb "$OUTDIR/hevc_ibbb.h265" 'I B B B B B B B'
assert_frame_types hevc_ra_ib "$OUTDIR/hevc_ra_ib.h265" 'I B B B B B B B'

assert_ref_frames h264_ipp_single "$OUTDIR/h264_ipp_single.h264" 1
assert_ref_frames h264_ipp "$OUTDIR/h264_ipp.h264" 2
assert_ref_frames h264_custom_multiref "$OUTDIR/h264_custom_multiref.h264" 3
assert_ref_frames h264_ra_ib "$OUTDIR/h264_ra_ib.h264" 4

hevc_single_refs="$(python3 "$SCRIPT_DIR/parse-hevc-refs.py" "$OUTDIR/hevc_ipp_single.h265")"
hevc_ipp_refs="$(python3 "$SCRIPT_DIR/parse-hevc-refs.py" "$OUTDIR/hevc_ipp.h265")"
hevc_ra_refs="$(python3 "$SCRIPT_DIR/parse-hevc-refs.py" "$OUTDIR/hevc_ra_ib.h265")"
echo "hevc_ipp_single_refs: $hevc_single_refs"
echo "hevc_ipp_refs: $hevc_ipp_refs"
echo "hevc_ra_refs: $hevc_ra_refs"
grep -q '"l0_active_refs": 1' <<<"$hevc_single_refs"
grep -q '"l0_active_refs": 2' <<<"$hevc_ipp_refs"
grep -q '"dpb_pictures": 5' <<<"$hevc_ra_refs"

hevc_merge_1="$(python3 "$SCRIPT_DIR/parse-hevc-slices.py" "$OUTDIR/hevc_max_merge_1.h265")"
hevc_merge_2="$(python3 "$SCRIPT_DIR/parse-hevc-slices.py" "$OUTDIR/hevc_max_merge_2.h265")"
echo "hevc_max_merge_1_slices: $hevc_merge_1"
echo "hevc_max_merge_2_slices: $hevc_merge_2"
grep -q '"five_minus_max_num_merge_cand": 4' <<<"$hevc_merge_1"
grep -q '"max_merge_candidates": 1' <<<"$hevc_merge_1"
grep -q '"five_minus_max_num_merge_cand": 3' <<<"$hevc_merge_2"
grep -q '"max_merge_candidates": 2' <<<"$hevc_merge_2"

hevc_strong_smooth_0="$(python3 "$SCRIPT_DIR/parse-hevc-slices.py" "$OUTDIR/hevc_strong_intra_smoothing_0.h265")"
hevc_strong_smooth_1="$(python3 "$SCRIPT_DIR/parse-hevc-slices.py" "$OUTDIR/hevc_strong_intra_smoothing_1.h265")"
echo "hevc_strong_intra_smoothing_0_sps: $hevc_strong_smooth_0"
echo "hevc_strong_intra_smoothing_1_sps: $hevc_strong_smooth_1"
grep -q '"strong_intra_smoothing_enabled_flag": 0' <<<"$hevc_strong_smooth_0"
grep -q '"strong_intra_smoothing_enabled_flag": 1' <<<"$hevc_strong_smooth_1"

assert_hevc_lossless
assert_no_lookahead_api
assert_no_quality_speed_preset_api
assert_no_alpha_av1_api
assert_no_444_source_format
assert_no_h264_interlaced_api
assert_reconfigure_marked_unsupported
assert_no_sequence_parameter_api
assert_h264_progressive_only h264_ipp "$OUTDIR/h264_ipp.h264"
assert_h264_progressive_only h264_ra_ib "$OUTDIR/h264_ra_ib.h264"
assert_hevc_single_temporal_id hevc_ra_ib "$OUTDIR/hevc_ra_ib.h265"
assert_hierarchical_temporal_ids h264 "$OUTDIR/h264_custom_hierarchical.h264"
assert_hierarchical_temporal_ids hevc "$OUTDIR/hevc_custom_hierarchical.h265"

echo "PASS: encoder feature probe finished. outputs: $OUTDIR"
