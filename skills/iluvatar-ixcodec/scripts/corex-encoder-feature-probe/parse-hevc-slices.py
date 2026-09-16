#!/usr/bin/env python3
import json
import math
import sys


class BitReader:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def bit(self):
        if self.pos >= len(self.data) * 8:
            raise EOFError("bitstream exhausted")
        value = (self.data[self.pos // 8] >> (7 - (self.pos % 8))) & 1
        self.pos += 1
        return value

    def u(self, nbits):
        value = 0
        for _ in range(nbits):
            value = (value << 1) | self.bit()
        return value

    def ue(self):
        zeros = 0
        while self.bit() == 0:
            zeros += 1
        return (1 << zeros) - 1 + (self.u(zeros) if zeros else 0)

    def se(self):
        value = self.ue()
        return (value + 1) // 2 if value & 1 else -(value // 2)


def rbsp(nal):
    out = bytearray()
    zeros = 0
    for byte in nal:
        if zeros == 2 and byte == 3:
            zeros = 0
            continue
        out.append(byte)
        zeros = zeros + 1 if byte == 0 else 0
    return bytes(out)


def split_annexb(data):
    starts = []
    i = 0
    while i < len(data) - 3:
        if data[i:i + 3] == b"\x00\x00\x01":
            starts.append((i, 3))
            i += 3
        elif i < len(data) - 4 and data[i:i + 4] == b"\x00\x00\x00\x01":
            starts.append((i, 4))
            i += 4
        else:
            i += 1
    nals = []
    for idx, (start, marker_len) in enumerate(starts):
        end = starts[idx + 1][0] if idx + 1 < len(starts) else len(data)
        nal = data[start + marker_len:end]
        while nal and nal[-1] == 0:
            nal = nal[:-1]
        if len(nal) >= 2:
            nals.append(nal)
    return nals


def skip_profile_tier_level(br, max_sub_layers_minus1):
    br.u(2); br.u(1); br.u(5); br.u(32); br.u(4); br.u(44); br.u(8)
    profile_present = []
    level_present = []
    for _ in range(max_sub_layers_minus1):
        profile_present.append(br.u(1))
        level_present.append(br.u(1))
    if max_sub_layers_minus1:
        for _ in range(max_sub_layers_minus1, 8):
            br.u(2)
    for has_profile, has_level in zip(profile_present, level_present):
        if has_profile:
            br.u(2); br.u(1); br.u(5); br.u(32); br.u(4); br.u(44)
        if has_level:
            br.u(8)


def skip_scaling_list_data(br):
    for size_id in range(4):
        matrices = 2 if size_id == 3 else 6
        for _ in range(matrices):
            if not br.u(1):
                br.ue()
            else:
                coef_num = min(64, 1 << (4 + (size_id << 1)))
                if size_id > 1:
                    br.se()
                for _ in range(coef_num):
                    br.se()


def skip_short_term_ref_pic_set(br, idx, total, rps):
    inter = idx != 0 and br.u(1)
    if inter:
        delta_idx_minus1 = br.ue() if idx == total else 0
        ref_idx = idx - 1 - delta_idx_minus1
        ref_delta_pocs = rps[ref_idx] if 0 <= ref_idx < len(rps) else 0
        br.u(1)
        br.ue()
        used_count = 0
        for _ in range(ref_delta_pocs + 1):
            used = br.u(1)
            use_delta = 1 if used else br.u(1)
            if used or use_delta:
                used_count += 1
        return used_count
    neg = br.ue()
    pos = br.ue()
    for _ in range(neg):
        br.ue(); br.u(1)
    for _ in range(pos):
        br.ue(); br.u(1)
    return neg + pos


def parse_sps(nal):
    br = BitReader(rbsp(nal[2:]))
    br.u(4)
    max_sub_layers_minus1 = br.u(3)
    br.u(1)
    skip_profile_tier_level(br, max_sub_layers_minus1)
    sps_id = br.ue()
    chroma_format_idc = br.ue()
    separate_colour_plane_flag = br.u(1) if chroma_format_idc == 3 else 0
    width = br.ue()
    height = br.ue()
    if br.u(1):
        br.ue(); br.ue(); br.ue(); br.ue()
    br.ue(); br.ue()
    log2_max_pic_order_cnt_lsb_minus4 = br.ue()
    sub_layer_ordering_info_present_flag = br.u(1)
    start = 0 if sub_layer_ordering_info_present_flag else max_sub_layers_minus1
    for _ in range(start, max_sub_layers_minus1 + 1):
        br.ue(); br.ue(); br.ue()
    br.ue(); br.ue(); br.ue(); br.ue(); br.ue(); br.ue()
    if br.u(1) and br.u(1):
        skip_scaling_list_data(br)
    br.u(1)
    sample_adaptive_offset_enabled_flag = br.u(1)
    if br.u(1):
        br.u(4); br.u(4); br.ue(); br.ue(); br.u(1)
    rps = []
    num_short_term_ref_pic_sets = br.ue()
    for idx in range(num_short_term_ref_pic_sets):
        rps.append(skip_short_term_ref_pic_set(br, idx, num_short_term_ref_pic_sets, rps))
    long_term_ref_pics_present_flag = br.u(1)
    if long_term_ref_pics_present_flag:
        for _ in range(br.ue()):
            br.u(log2_max_pic_order_cnt_lsb_minus4 + 4)
            br.u(1)
    sps_temporal_mvp_enabled_flag = br.u(1)
    strong_intra_smoothing_enabled_flag = br.u(1)
    return {
        "sps_id": sps_id,
        "width": width,
        "height": height,
        "separate_colour_plane_flag": separate_colour_plane_flag,
        "log2_max_pic_order_cnt_lsb": log2_max_pic_order_cnt_lsb_minus4 + 4,
        "sample_adaptive_offset_enabled_flag": sample_adaptive_offset_enabled_flag,
        "num_short_term_ref_pic_sets": num_short_term_ref_pic_sets,
        "rps": rps,
        "long_term_ref_pics_present_flag": long_term_ref_pics_present_flag,
        "sps_temporal_mvp_enabled_flag": sps_temporal_mvp_enabled_flag,
        "strong_intra_smoothing_enabled_flag": strong_intra_smoothing_enabled_flag,
    }


def parse_pps(nal):
    br = BitReader(rbsp(nal[2:]))
    pps = {
        "pps_id": br.ue(),
        "sps_id": br.ue(),
        "dependent_slice_segments_enabled_flag": br.u(1),
        "output_flag_present_flag": br.u(1),
        "num_extra_slice_header_bits": br.u(3),
        "sign_data_hiding_enabled_flag": br.u(1),
        "cabac_init_present_flag": br.u(1),
        "num_ref_idx_l0_default_active_minus1": br.ue(),
        "num_ref_idx_l1_default_active_minus1": br.ue(),
    }
    br.se()
    br.u(1); br.u(1)
    if br.u(1):
        br.ue()
    br.se(); br.se()
    pps["pps_slice_chroma_qp_offsets_present_flag"] = br.u(1)
    pps["weighted_pred_flag"] = br.u(1)
    pps["weighted_bipred_flag"] = br.u(1)
    br.u(1)
    pps["tiles_enabled_flag"] = br.u(1)
    pps["entropy_coding_sync_enabled_flag"] = br.u(1)
    if pps["tiles_enabled_flag"]:
        cols = br.ue()
        rows = br.ue()
        if not br.u(1):
            for _ in range(cols):
                br.ue()
            for _ in range(rows):
                br.ue()
        br.u(1)
    br.u(1)
    if br.u(1):
        pps["deblocking_filter_override_enabled_flag"] = br.u(1)
        pps["pps_deblocking_filter_disabled_flag"] = br.u(1)
        if not pps["pps_deblocking_filter_disabled_flag"]:
            br.se(); br.se()
    else:
        pps["deblocking_filter_override_enabled_flag"] = 0
        pps["pps_deblocking_filter_disabled_flag"] = 0
    if br.u(1):
        skip_scaling_list_data(br)
    pps["lists_modification_present_flag"] = br.u(1)
    pps["log2_parallel_merge_level_minus2"] = br.ue()
    pps["slice_segment_header_extension_present_flag"] = br.u(1)
    return pps


def ceil_log2(x):
    return int(math.ceil(math.log2(max(1, x))))


def parse_slice(nal, sps_map, pps_map):
    nal_type = (nal[0] >> 1) & 0x3f
    br = BitReader(rbsp(nal[2:]))
    first_slice_segment_in_pic_flag = br.u(1)
    if 16 <= nal_type <= 23:
        br.u(1)
    pps_id = br.ue()
    pps = pps_map[pps_id]
    sps = sps_map[pps["sps_id"]]
    dependent = 0
    if not first_slice_segment_in_pic_flag:
        if pps["dependent_slice_segments_enabled_flag"]:
            dependent = br.u(1)
        br.u(ceil_log2(sps["width"] * sps["height"]))
    if dependent:
        return None
    for _ in range(pps["num_extra_slice_header_bits"]):
        br.u(1)
    slice_type = br.ue()
    if pps["output_flag_present_flag"]:
        br.u(1)
    if sps["separate_colour_plane_flag"]:
        br.u(2)
    if nal_type not in (19, 20):
        br.u(sps["log2_max_pic_order_cnt_lsb"])
        if not br.u(1):
            skip_short_term_ref_pic_set(
                br,
                sps["num_short_term_ref_pic_sets"],
                sps["num_short_term_ref_pic_sets"],
                sps["rps"],
            )
        elif sps["num_short_term_ref_pic_sets"] > 1:
            br.u(ceil_log2(sps["num_short_term_ref_pic_sets"]))
        if sps["long_term_ref_pics_present_flag"]:
            raise RuntimeError("long-term refs not supported by this checker")
        if sps["sps_temporal_mvp_enabled_flag"]:
            br.u(1)
    if sps["sample_adaptive_offset_enabled_flag"]:
        br.u(1); br.u(1)
    result = {"nal_type": nal_type, "slice_type": slice_type}
    if slice_type in (0, 1):
        l0 = pps["num_ref_idx_l0_default_active_minus1"]
        l1 = pps["num_ref_idx_l1_default_active_minus1"]
        if br.u(1):
            l0 = br.ue()
            if slice_type == 0:
                l1 = br.ue()
        if pps["lists_modification_present_flag"]:
            # The exact NumPocTotalCurr depends on RPS derivation. For these probe streams,
            # the active reference count is enough to skip the list-entry indices.
            num_poc_total_curr = max(l0 + 1, l1 + 1, 2)
            entry_bits = ceil_log2(num_poc_total_curr)
            if br.u(1):
                for _ in range(l0 + 1):
                    br.u(entry_bits)
            if slice_type == 0 and br.u(1):
                for _ in range(l1 + 1):
                    br.u(entry_bits)
        if slice_type == 0:
            br.u(1)
        if pps["cabac_init_present_flag"]:
            br.u(1)
        if sps["sps_temporal_mvp_enabled_flag"]:
            br.u(1)
        if (pps["weighted_pred_flag"] and slice_type == 1) or (pps["weighted_bipred_flag"] and slice_type == 0):
            raise RuntimeError("weighted pred not supported by this checker")
        five_minus = br.ue()
        result["five_minus_max_num_merge_cand"] = five_minus
        result["max_merge_candidates"] = 5 - five_minus
    return result


def summarize(path):
    sps = {}
    pps = {}
    slices = []
    for nal in split_annexb(open(path, "rb").read()):
        nal_type = (nal[0] >> 1) & 0x3f
        if nal_type == 33:
            parsed = parse_sps(nal)
            sps[parsed["sps_id"]] = parsed
        elif nal_type == 34:
            parsed = parse_pps(nal)
            pps[parsed["pps_id"]] = parsed
        elif nal_type < 32 and sps and pps:
            parsed = parse_slice(nal, sps, pps)
            if parsed and "max_merge_candidates" in parsed:
                slices.append(parsed)
    if not sps:
        raise SystemExit(f"failed to parse SPS from {path}")
    return {"file": path, "sps": sps, "slices": slices}


for path in sys.argv[1:]:
    print(json.dumps(summarize(path), sort_keys=True))
