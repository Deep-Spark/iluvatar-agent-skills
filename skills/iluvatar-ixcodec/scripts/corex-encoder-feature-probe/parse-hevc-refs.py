#!/usr/bin/env python3
import json
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
    br.u(2)
    br.u(1)
    br.u(5)
    br.u(32)
    br.u(4)
    br.u(44)
    br.u(8)
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
            br.u(2)
            br.u(1)
            br.u(5)
            br.u(32)
            br.u(4)
            br.u(44)
        if has_level:
            br.u(8)


def parse_sps(nal):
    br = BitReader(rbsp(nal[2:]))
    br.u(4)
    max_sub_layers_minus1 = br.u(3)
    br.u(1)
    skip_profile_tier_level(br, max_sub_layers_minus1)
    sps_id = br.ue()
    chroma_format_idc = br.ue()
    if chroma_format_idc == 3:
        br.u(1)
    width = br.ue()
    height = br.ue()
    if br.u(1):
        br.ue(); br.ue(); br.ue(); br.ue()
    br.ue(); br.ue()
    br.ue() + 4
    sub_layer_ordering_info_present_flag = br.u(1)
    start = 0 if sub_layer_ordering_info_present_flag else max_sub_layers_minus1
    ordering = []
    for _ in range(start, max_sub_layers_minus1 + 1):
        ordering.append({
            "max_dec_pic_buffering_minus1": br.ue(),
            "max_num_reorder_pics": br.ue(),
            "max_latency_increase_plus1": br.ue(),
        })
    current = ordering[-1]
    return {"sps_id": sps_id, "width": width, "height": height, **current}


def parse_pps(nal):
    br = BitReader(rbsp(nal[2:]))
    return {
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


def summarize(path):
    sps = {}
    pps = {}
    for nal in split_annexb(open(path, "rb").read()):
        nal_type = (nal[0] >> 1) & 0x3f
        if nal_type == 33:
            parsed = parse_sps(nal)
            sps[parsed["sps_id"]] = parsed
        elif nal_type == 34:
            parsed = parse_pps(nal)
            pps[parsed["pps_id"]] = parsed
    if not sps or not pps:
        raise SystemExit(f"failed to parse SPS/PPS from {path}")
    first_pps = next(iter(pps.values()))
    linked_sps = sps[first_pps["sps_id"]]
    return {
        "file": path,
        "sps": linked_sps,
        "pps": first_pps,
        "l0_active_refs": first_pps["num_ref_idx_l0_default_active_minus1"] + 1,
        "l1_active_refs": first_pps["num_ref_idx_l1_default_active_minus1"] + 1,
        "dpb_pictures": linked_sps["max_dec_pic_buffering_minus1"] + 1,
    }


for path in sys.argv[1:]:
    print(json.dumps(summarize(path), sort_keys=True))
