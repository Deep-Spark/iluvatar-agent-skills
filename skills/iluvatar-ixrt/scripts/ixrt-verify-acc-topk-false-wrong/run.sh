#!/bin/bash
cd "$(dirname "$0")"
export LD_LIBRARY_PATH=/usr/local/corex/lib64:$LD_LIBRARY_PATH
python3 gen_topk_onnx.py

# 精度验证指令 报告精度异常
ixrtexec --onnx=topk.onnx --precision fp16 --verify_acc --ort_cpu

# 分析以及影响
python3 verify_acc_topk_false_wrong.py