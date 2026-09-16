#!/usr/bin/env python3
# 解析 ixrtexec --run_profiler 导出的逐层 CSV，按算子类别聚合耗时。
# 用法：python3 profile_layers.py a.csv [b.csv ...]
# CSV 列：Layer Name, Time(ms), Avg. Time(ms), Median Time(ms), Time(%) Band Width(GB/s), Input Shapes
import csv
import collections
import sys

# 层名里命中的关键字 -> 类别（顺序敏感：先命中的优先）
CATS = ["reformat", "quantize", "dequant", "convert", "cast", "copy",
        "conv", "silu", "sigmoid", "mul", "add", "concat", "resize",
        "upsample", "maxpool", "split", "slice", "transpose", "reshape"]


def categorize(name):
    n = name.lower()
    for k in CATS:
        if k in n:
            return k
    return "other"


def summarize(path):
    rows = [r for r in csv.DictReader(open(path)) if r.get("Layer Name")]
    by_cat_time = collections.Counter()
    by_cat_cnt = collections.Counter()
    total = 0.0
    for r in rows:
        try:
            t = float(r["Avg. Time(ms)"])
        except (KeyError, ValueError, TypeError):
            t = 0.0
        c = categorize(r["Layer Name"])
        by_cat_time[c] += t
        by_cat_cnt[c] += 1
        total += t
    return rows, by_cat_time, by_cat_cnt, total


def main():
    paths = sys.argv[1:]
    if not paths:
        print("用法: python3 profile_layers.py a.csv [b.csv ...]")
        sys.exit(1)
    for path in paths:
        rows, tt, cc, total = summarize(path)
        print(f"===== {path}  total {total:.2f} ms / {len(rows)} layers =====")
        for c, t in tt.most_common():
            pct = (t / total * 100) if total else 0.0
            print(f"  {t:8.3f} ms  {pct:5.1f}%  x{cc[c]:<4d} {c}")
        print()


if __name__ == "__main__":
    main()
