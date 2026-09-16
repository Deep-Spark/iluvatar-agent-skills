#!/bin/bash
cd "$(dirname "$0")"

python3 gen_topk_onnx.py

# 精度验证指令 报告精度异常（默认逐元素比 → values+indices 都 FAILED）
polygraphy run topk.onnx --trt --onnxrt --fp16

# 对 TopK 的索引输出放宽（--index-tolerance 需配 --compare indices 才生效）：
# indices 项 PASS，但 values(float) 被迫按索引比、仍 FAIL → 整体仍 FAILED
polygraphy run topk.onnx --trt --onnxrt --fp16 --compare indices --index-tolerance indices:50

# 正解：per-output 比较脚本（values 用 atol、indices 用 index-tolerance）→ 整体 PASS
polygraphy run topk.onnx --trt --onnxrt --fp16 --compare-func-script compare_topk.py

# 分析以及影响
python3 verify_acc_topk_false_wrong.py
