#!/usr/bin/env python3
import json
import sys


def split_annexb(data):
    starts = []
    i = 0
    while i < len(data) - 3:
        if data[i:i + 4] == b"\x00\x00\x00\x01":
            starts.append((i, 4))
            i += 4
        elif data[i:i + 3] == b"\x00\x00\x01":
            starts.append((i, 3))
            i += 3
        else:
            i += 1
    nals = []
    for index, (start, marker_len) in enumerate(starts):
        end = starts[index + 1][0] if index + 1 < len(starts) else len(data)
        nal = data[start + marker_len:end]
        if nal:
            nals.append(nal)
    return nals


def h264_temporal_ids(nals):
    values = set()
    for nal in nals:
        if (nal[0] & 0x1f) != 14 or len(nal) < 4:
            continue
        if not (nal[1] & 0x80):
            continue
        values.add((nal[3] >> 5) & 0x07)
    return sorted(values)


def hevc_temporal_ids(nals):
    return sorted({(nal[1] & 0x07) - 1
                   for nal in nals
                   if len(nal) >= 2 and ((nal[0] >> 1) & 0x3f) < 32})


if len(sys.argv) != 3 or sys.argv[1] not in ("h264", "hevc"):
    raise SystemExit(f"usage: {sys.argv[0]} h264|hevc bitstream")

codec, path = sys.argv[1:]
nals = split_annexb(open(path, "rb").read())
temporal_ids = h264_temporal_ids(nals) if codec == "h264" else hevc_temporal_ids(nals)
print(json.dumps({"codec": codec, "temporal_ids": temporal_ids}, sort_keys=True))
