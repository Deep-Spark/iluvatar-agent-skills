#!/usr/bin/env python3
import sys


if len(sys.argv) != 6:
    raise SystemExit(f"usage: {sys.argv[0]} width height i420 nv12 nv21")

width, height = map(int, sys.argv[1:3])
i420 = open(sys.argv[3], "rb").read()
nv12 = open(sys.argv[4], "rb").read()
nv21 = open(sys.argv[5], "rb").read()
y_size = width * height
uv_size = y_size // 4
expected_size = y_size + uv_size * 2

if not (len(i420) == len(nv12) == len(nv21) == expected_size):
    raise SystemExit(
        f"unexpected sizes: i420={len(i420)} nv12={len(nv12)} nv21={len(nv21)}")

y = i420[:y_size]
u = i420[y_size:y_size + uv_size]
v = i420[y_size + uv_size:]
expected_nv12 = y + bytes(value for pair in zip(u, v) for value in pair)
expected_nv21 = y + bytes(value for pair in zip(v, u) for value in pair)

if nv12 != expected_nv12:
    raise SystemExit("NV12 output does not match I420 Y/U/V planes")
if nv21 != expected_nv21:
    raise SystemExit("NV21 output does not match I420 Y/U/V planes")

print(f"layout_content: PASS ({width}x{height}, I420/NV12/NV21)")
