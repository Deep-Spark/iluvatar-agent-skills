# ixrt-verify-acc-topk-false-wrong

## 目的

复现并解释 **`ixrtexec --verify_acc` 对含 TopK（及 NMS/GatherElements/选取类算子）的模型假报 `WRONG`**，
并证明三点：① 这是 fp16 量化的固有现象、**ixrt 和 NV TensorRT 都有**；② 值本身精确，差异只在
rank-K 截断边界的最低分元素；③ **不影响带 conf 阈值的实际模型输出**。被测点：单个 `TopK(MAX, k=300)`。

配套 `references/ixrtexec.md` 的「精度验证」节。

## 结果

`ixrtexec --onnx=topk.onnx --precision fp16 --verify_acc --ort_cpu`（默认随机输入）逐输出对比：

```
+----------+-------------------------+----------------------------------------+
| Result   | Operator-->Output       | Comparison Results                     |
+==========+=========================+========================================+
| RIGHT    | values  (1,300) float32 | Relative Diff Max  4.75e-05            |
|          |                         | Absolute Diff Max  2.38e-04            |
|          |                         | Cosine Similarity  0.999999996         |   <- 分数本身几乎精确
+----------+-------------------------+----------------------------------------+
| WRONG    | indices (1,300) int64   | Relative Diff Max  0.249               |
|          | (ONNXTRT_castHelper)    | Absolute Diff Max  7407                |
|          |                         | Cosine Similarity  0.9408              |   <- 仅索引在并列边界对不上
+----------+-------------------------+----------------------------------------+
Compared with onnxruntime, result calculated from IxRT is WRONG
```

总判 `WRONG` **只来自 `indices`**（`values` 是 `RIGHT`，余弦 0.999999996）——即分数算得对、
只是 top-K 边界并列项的**索引取舍**与 onnxruntime 不同。根因 + 双平台一致 + 不影响实际输出：

```
platform = ixrt
  FP32 engine TopK vs fp32-ref: set_overlap=300/300  [match]
  FP16 engine TopK vs fp32-ref: set_overlap=298/300  [DIFFERS]   <- ixrt 与 NV TensorRT 都 298/300
  差异元素 score≈9.637-9.644，全部紧贴 top-K 截断点 9.6429 -> 真实检测里 << conf 阈值会被丢弃，最终检测不变
```

**假阳性的本质**：`values`（数值）`RIGHT` 而 `indices`（选取）`WRONG`，差异只在 fp16 并列的 rank-K 边界，
且 ixrt 与 NV TensorRT 完全一致（都 298/300）——`WRONG` 是 verify_acc 对「fp16 并列 + TopK 边界取舍」
的误判，而非 ixrt 算子计算错。

## 触发配方

分数相邻 gap≈1.2e-3，靠近大数值处 fp16 ULP(≈8e-3) **>** gap → fp16 把一批相邻分数舍成**并列**；
rank-K 截断点恰落在并列簇里，fp16 引擎选中的 300 个里有 2 个与「对原始 fp32 分数 argsort」的
参考不同 → `--verify_acc`（对照 onnxruntime fp32）判 `WRONG`。

```bash
# CLI 症状（corex）：最小 TopK onnx → fp16 引擎 vs onnxruntime fp32
ixrtexec --onnx=topk.onnx --precision fp16 --verify_acc --ort_cpu      # → WRONG
# 根因：FP32 引擎 set_overlap=300/300（匹配）；FP16 引擎=298/300（边界差 2 个）
```

## TensorRT vs ixRT

**① 根因（同脚本双平台，`verify_acc_topk_false_wrong.py`）：**

| TopK vs fp32 参考 | TensorRT (A10) | ixRT (MR-V100) |
|---|---|---|
| FP32 引擎 | set_overlap **300/300** | **300/300** |
| FP16 引擎 | set_overlap **298/300** | **298/300** |

两平台 fp16 的边界偏离完全一致（都 298/300）。

**② 工具层面（对同一个 `topk.onnx`，fp16，默认随机输入）：两平台的精度对比工具都假报 mismatch：**

| 工具 | 命令 | 结果 |
|---|---|---|
| ixRT | `ixrtexec --onnx=topk.onnx --precision fp16 --verify_acc --ort_cpu` | **WRONG** |
| TensorRT | `polygraphy run topk.onnx --trt --onnxrt --fp16` | **FAILED**（`values` + `indices` 均 mismatch，Pass Rate 0%）|

→ 这是 fp16-vs-fp32 的通用量化现象、**不是 ixrt 缺陷**；底层 fp16 TopK 行为与上层校验工具的判罚两平台一致
（`--verify_acc` 是 ixrtexec 独有，NV 侧等价为独立工具 polygraphy）。

## 为什么不影响实际模型输出

差异的元素分数都紧贴 rank-K 截断点（本例 ≈9.643，是被选中 300 个里的最低档）。真实检测器里
TopK 在 8400 候选中选 top-300，rank-300 的分数远低于 conf 阈值（≪0.4），这些边界元素**会被阈值
直接丢弃**，不会成为最终检测框。实测佐证：YOLOv10n / YOLO26n 端到端，**ixrt-cpp 与 NV-cpp 选出
完全相同的检测框**，置信度仅差 ~0.001–0.008。

## 用正确的比较即可消除这个假阳性（`compare_topk.py`）

NV 侧 polygraphy 提供了验证途径，但 `--compare` 只能**全局选一种**比较方法，所以双输出 topk 无法用单条命令让 `values`(float) 和 `indices`(int) 同时过：

```
# (1) 默认逐元素比 → values+indices 都 FAILED
polygraphy run topk.onnx --trt --onnxrt --fp16
# (2) --compare indices --index-tolerance indices:50 → indices PASS，但 values 被迫按索引比仍 FAIL
polygraphy run topk.onnx --trt --onnxrt --fp16 --compare indices --index-tolerance indices:50
# (3) per-output 比较脚本 → 整体 PASS, Pass Rate 100%
polygraphy run topk.onnx --trt --onnxrt --fp16 --compare-func-script compare_topk.py
```

### `compare_topk.py` 的原理

**① `--compare-func-script` 逃生口**：polygraphy 跑完 TRT 与 onnxruntime 后，把两边输出（各是一个
`IterationResult`，本质 `{输出名: ndarray}`）传给脚本里的 `compare_outputs(run0, run1)`，它返回
`{输出名: 是否通过}`，全 True 才整体 PASS——从而绕开 `--compare` 的"全局单方法"限制，可**按输出分别比**。

**② 按输出拆开、各套各的 CompareFunc**（复用 polygraphy 自己的比较器，不自造）：

```python
def _sub(run, name):  # 摘出单个输出，构造只含它的 IterationResult
    return IterationResult(outputs={name: run[name]}, runner_name=run.runner_name)

res.update(CompareFunc.simple(atol=1e-2, rtol=1e-2)(_sub(run0,"values"),  _sub(run1,"values")))
res.update(CompareFunc.indices(index_tolerance=50)(_sub(run0,"indices"), _sub(run1,"indices")))
```

**③ 两个输出用不同语义**（关键）：

| 输出 | 性质 | 比较器 | 判据 |
|---|---|---|---|
| `values` | 选中的**分数**(float) | `simple(atol/rtol)` | 逐元素 `\|a-b\| ≤ atol + rtol·\|b\|`，吸收 fp16 舍入（实测差 ~2.4e-4 ≪ 0.01）|
| `indices` | 选中的**位置编号**(int) | `indices(index_tolerance=50)` | 对每个值找它在另一边的位置，`\|pos1-pos0\| ≤ 50` 即过 |

`indices` 不能用逐元素相等比：fp16 把并列分数舍成相等后，TopK 对并列项的排序合法地变了，被选中的
索引只是在排名里"挪了几个位置"；`CompareFunc.indices` 量的正是这个**位置位移**，≤50 就放过。
`values` 是纯数值，按 atol 比即可。

→ 给每个输出对的判据后 **PASSED | All outputs matched, Pass Rate 100%**。这正面证明：(1)/(2) 的
FAILED 是「fp16 分数舍入 + TopK 并列索引重排」的**假阳性**——是工具默认判据不合适，不是 ixrt/TensorRT 算错。


## 运行

`run.sh` 与 `run_nv.sh` 都会先用 `gen_topk_onnx.py` 生成 `topk.onnx`（确定性，不入仓），
再共用同一份根因脚本 `verify_acc_topk_false_wrong.py`。
