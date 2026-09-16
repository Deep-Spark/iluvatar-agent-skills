"""
polygraphy `--compare-func-script`：对 topk.onnx 做 per-output 比较，绕过 `--compare` 只能全局
选一种方法的限制（见 references/ixrtexec.md 精度验证节、polygraphy compare.py 源码）：

  values  -> CompareFunc.simple(atol/rtol)          吸收 fp16 分数舍入（~2.4e-4）
  indices -> CompareFunc.indices(index_tolerance)   容忍 TopK 并列边界的索引重排（位置距离）

两个输出各按各自语义判 → 整体 PASS。证明：TopK 在 `--verify_acc` / 默认 polygraphy 下报的
FAILED/WRONG，是「fp16 分数舍入 + 并列索引重排」的假阳性，用正确的 per-output 比较即可抑制。

用法:
  polygraphy run topk.onnx --trt --onnxrt --fp16 --compare-func-script compare_topk.py
"""
from collections import OrderedDict

from polygraphy.comparator import CompareFunc, IterationResult

VAL_ATOL, VAL_RTOL = 1e-2, 1e-2   # values: fp16 分数容差
IDX_TOL = 50                      # indices: 并列重排的位置容差


def _sub(run, name):
    # 取单个输出，构造只含该输出的 IterationResult，以便对它单独套用某个 CompareFunc
    return IterationResult(outputs={name: run[name]}, runner_name=run.runner_name)


def compare_outputs(run0, run1):
    res = OrderedDict()
    if "values" in run0:
        res.update(CompareFunc.simple(atol=VAL_ATOL, rtol=VAL_RTOL)(_sub(run0, "values"), _sub(run1, "values")))
    if "indices" in run0:
        res.update(CompareFunc.indices(index_tolerance=IDX_TOL)(_sub(run0, "indices"), _sub(run1, "indices")))
    return res
