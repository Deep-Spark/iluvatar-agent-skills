# 命令行工具：`ixrtexec`（trtexec 的对应物）

NV 的 `trtexec` 在 Corex 上对应 **`ixrtexec`**（`/usr/local/bin/ixrtexec` 是 Python 入口脚本）。两者都能「onnx → 构建引擎 → 跑基准」，但**参数名风格不同**：trtexec 常用 camelCase（如 `--saveEngine=x`），ixrtexec 用 snake_case（如 `--save_engine x` 或 `--save_engine=x`）。默认行为一致：构建后会跑性能基准并打印 fps/qps。

## 参数对照表

| 用途 | trtexec (NV) | ixrtexec (Corex) | 备注 |
|---|---|---|---|
| 输入 onnx | `--onnx=m.onnx` | `--onnx=m.onnx` | 同 |
| 存引擎 | `--saveEngine=m.plan` | `--save_engine=m.trt` | 名字不同 |
| 载引擎 | `--loadEngine=m.plan` | `--load_engine=m.trt` | |
| **精度** | `--fp16` / `--int8` / `--bf16` / `--best` | `--precision fp16`（可多值 `--precision fp16 int8`） | trtexec 是多个开关；ixrtexec 是**单参数取值列表** [int8/fp16/bf16/fp32] |
| 动态 shape | `--minShapes=` / `--optShapes=` / `--maxShapes=` | `--min_shape` / `--opt_shape` / `--max_shape`（+ `--shapes` 设静态） | 值格式同为 `name:1x3x224x224` |
| IO 类型 | `--inputIOFormats=fp16:chw` | `--input_types input0:float16` / `--output_types` | ixrtexec **只设 dtype、不设 layout** |
| 迭代/预热 | `--iterations=N` / `--warmUp=ms` | `--iterations N` / `--warmUp ms` | 语义同 |
| profiling | `--dumpProfile --exportProfile=f.json` | `--run_profiler --export_profiler=f.csv` | ixrt 导 CSV |
| timing cache | `--timingCacheFile=f` | `--timingCacheFile=f` | 同名 |
| builder 优化级 | `--builderOptimizationLevel=N` | `--builderOptimizationLevel=N`（0–5，默认 3） | 同名；正常转换/复现/交叉验证都**保持默认**，不要主动显式传 |
| 强类型网络 | `--stronglyTyped` | `--strongly_typed` | |
| 加载输入 | `--loadInputs=name:f` | `--load_inputs name:f.dat`（默认随机） | 二进制 raw |
| 日志级别 | `--verbose` | `--log_level verbose` | |
| 指定 GPU | `--device=N` | `--gpus N [...]` | ixrt 支持多卡列表 |
| INT8 校准 | `--calib=cache` | **无在线校准**，用 `--quant_file q.json`（离线量化表） | 见 [`ixrt-int8-quant.md`](./ixrt-int8-quant.md) |
| plugin | `--plugins=lib.so` | `--plugins lib.so [...]` | |
| 算子支持查询 | （无） | `--support {pipe,pretty}` | 当前版本未实现支持列表输出；不要把它当能力查询 |
| 导出引擎图 | `--exportLayerInfo=` | `--dump_graph out.onnx` | ixrt 导成 onnx |
| **精度验证** | （无内置，需 polygraphy） | `--verify_acc` 一族 | **ixrtexec 独有**，见下「精度验证」节 |

> 模型转换规则：`ixrtexec` 构建 engine 时默认不传 `--builderOptimizationLevel`；Python/C++ builder 代码也默认不要设置 optimization level。正常转换、复现、交叉验证都使用默认 level。只有当默认 level 已经暴露 builder/runtime 问题，或明确要定位“默认 level 为什么失败”时，才把修改 optimization level 当作诊断变量临时尝试，并在结果里说明它不是正式转换配置。不要为了省构建时间、固定日志、加速 repro 或让命令看起来更可控而改 level。

> 构建很慢时：trtexec/ixrtexec 都会做 tactic 自动调优。优先复用 `--timingCacheFile` 或缩小调试模型；不要把降低 `builderOptimizationLevel` 当作常规转换捷径。

## 典型用法

```bash
# 构建 fp16 引擎（最常用）
ixrtexec --onnx=yolo11n.onnx --save_engine=yolo11n.trt --precision fp16
# 动态 batch
ixrtexec --onnx=m.onnx --save_engine=m.trt --precision fp16 \
         --min_shape images:1x3x640x640 --opt_shape images:8x3x640x640 --max_shape images:16x3x640x640
# 当前 --support 不输出支持列表；算子是否可用以 parser/build 实测为准
```

## 精度验证：`ixrtexec --verify_acc`

NV 的 trtexec **没有**内置精度验证（要另用 polygraphy）；ixrtexec **内置**了一个：用 **onnxruntime 当参考**，对 IxRT 引擎**逐层**比对 余弦相似度 / 最大差 / 差值和 / 平均相对差，最后给 `RIGHT` / `WRONG` 总判。是量化「ixrt 到底掉多少精度」的首选工具。

### 用法

```bash
# fp16 引擎 vs onnxruntime-CPU(fp32) 的逐层精度
ixrtexec --onnx=m.onnx --precision fp16 --verify_acc --ort_cpu
```

| 选项 | 作用 |
|---|---|
| `--verify_acc` | 开启精度验证（参考框架目前只支持 onnxruntime） |
| `--ort_cpu` | onnxruntime 跑 CPU（纯 fp32 参考）；不加则用 CUDA EP |
| `--cosine_sim 0.999` | 余弦阈值，越大越严（不设 `--diff_*` 时只用余弦判定） |
| `--diff_max` / `--diff_sum` / `--diff_rel_avg` | 绝对最大差 / 差值和 / 平均相对差阈值，越大越宽松 |
| `--load_inputs name:x.dat` | 用**真实输入**替代默认随机输入（对含 TopK 的模型很关键，见下） |
| `--only_verify_outputs` | 只比最终输出（默认比所有层） |
| `--ort_onnx orig.onnx` | 当喂给 IxRT 的是 engine/QDQ 格式时，用原始 onnx 给 onnxruntime |
| `--save_verify_data dir` | 保留逐层中间数据（默认跑完删） |
| `--inject_tensors` / `--watch` | 注入指定边输入 / 跟踪某条边（查显存踩踏） |

### 输出解读

逐层一张表：`status | 层名 | (Relative Diff Max / Absolute Diff Max / Absolute Diff Sum / Cosine Similarity)`，末尾 `result calculated from IxRT is RIGHT/WRONG`。例（yolo11n 最终输出 `output0[1,84,8400]`，fp16 vs ort-cpu）：

```
Cosine Similarity   0.99999987     ← 方向几乎完全一致
Relative Diff Max   7.78e-05
Absolute Diff Max   3.245          ← 个别大数值坐标元素
Absolute Diff Sum   3226 / 705600  ← 平均绝对差 ≈ 0.0046
```

**结论：标准 backbone / 卷积检测头 / cls / seg / pose 的 ixrt fp16 几乎无损（余弦 7 个 9）。**

### ⚠️ 陷阱：含 TopK / GatherElements / NMS / ArgMax 的模型会假报 WRONG

`--verify_acc` 默认随机输入下分数可能近似并列，onnxruntime 与 IxRT 的选取/排序不同 → 选取类输出余弦暴跌、误判 `WRONG`。当前 TopK case 中，FP32 engine 与 fp32 reference 的集合完全一致，FP16 engine 在 rank-K 截断边界出现 298/300 的集合重合；NV 侧 `polygraphy run topk.onnx --trt --onnxrt --fp16` 对同一模型也会默认判 `FAILED`。**这不是普通逐元素数值 bug，而是指标对选取类算子的固有假阳性**。

排查：① `--load_inputs` 喂真实输入；② 比 fp16 vs fp32 cosine（fp32 仍低即选取不稳定，非数值错）；③ topk 列表 cosine 0.9x 不代表检测错，交叉核对实际检测结果。

完整复现 + 「不影响实际输出」证明 + 用 per-output 比较消除假阳性，见 [`scripts/ixrt-verify-acc-topk-false-wrong/`](../scripts/ixrt-verify-acc-topk-false-wrong/)。

### NVIDIA 侧等价：polygraphy

`ixrtexec --verify_acc` 是 ixrtexec 把「构建 + 精度对比」二合一；NVIDIA 侧 trtexec **只构建/跑基准、无精度对比**，等价能力由**单独的 `polygraphy` 工具**提供（TRT 自带）：它同时跑 TRT 引擎与 onnxruntime 并逐输出比对。

```bash
# ≈ ixrtexec --onnx=m.onnx --precision fp16 --verify_acc --ort_cpu
polygraphy run m.onnx --trt --onnxrt --fp16 --atol 1e-2 --rtol 1e-2
#   --trt --onnxrt   两个 runner：构建 TRT 引擎 + 跑 onnxruntime(默认 CPU)
#   --fp16           TRT 以 fp16 构建（对应 --precision fp16）
#   --atol/--rtol    绝对/相对容差（对应 --verify_acc 的 --diff_max 等）
#   --trt-min/opt/max-shapes name:1x3x640x640   动态 shape
```

输出逐输出打印 `PASSED/FAILED | Output: 'y' | Difference is within tolerance (rel=…, abs=…)` + Error Metrics，末尾 `PASSED/FAILED` 总判——与 `--verify_acc` 的 RIGHT/WRONG 对应。

**抑制 TopK 索引假阳性**：polygraphy 的 `--index-tolerance N` 量的是「值的位置位移」，但**必须配 `--compare indices`**（否则被忽略），而 `--compare` 是全局单方法——所以对 `values`(float)+`indices`(int) 双输出的 topk 模型，`--index-tolerance` 只能让 indices 过、values 反被按索引比而挂，**整体仍 FAILED**。正解是 `--compare-func-script` 写 per-output 比较函数（`values` 用 `CompareFunc.simple(atol)`、`indices` 用 `CompareFunc.indices(index_tolerance)`）→ 整体 PASS。可跑示例见 [`scripts/ixrt-verify-acc-topk-false-wrong/compare_topk.py`](../scripts/ixrt-verify-acc-topk-false-wrong/)。

| | Corex / IxRT | NVIDIA |
|---|---|---|
| 构建 + 基准 | `ixrtexec` | `trtexec` |
| 精度对比 vs onnxruntime | `ixrtexec --verify_acc`（同一工具内置） | `polygraphy run --trt --onnxrt`（独立工具） |
| TopK/ArgMax 索引假阳性的处理 | 交叉核对实际输出（无专项开关） | `--compare-func-script` per-output 比较（`--index-tolerance` 单用只够单输出） |
