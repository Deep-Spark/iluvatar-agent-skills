# 用 NVIDIA Model Optimizer 导出 ONNX QDQ 量化模型（喂给 IxRT 显式量化）

这是 [`onnx-qdq-export.md`](onnx-qdq-export.md)（ONNX Runtime 量化工具路线）的**第二条后端**：用 **NVIDIA Model Optimizer（modelopt）** 的 `modelopt.onnx.quantization.quantize` 生成带 `QuantizeLinear`/`DequantizeLinear`（Q/DQ）的 ONNX，再交给 `ixrtexec --onnx=` / ixrt 解析器消费。两条路线产物都是 QDQ ONNX、IxRT 端用法完全一样，区别只在量化器本身。可跑实现见本目录 [`scripts/onnx-qdq-export/export_modeopt.py`](../scripts/onnx-qdq-export/export_modeopt.py)。

参考：<https://nvidia.github.io/Model-Optimizer/guides/_onnx_quantization.html>（modelopt 为上游工具，与 Corex/IxRT 平台无关；下述 API 名/参数以官方文档为准，可能随版本调整，落地前请复核签名）。

## 与 ORT 量化路线的区别

| | ONNX Runtime（`export.py`） | Model Optimizer（`export_modeopt.py`） |
|---|---|---|
| 入口 | `onnxruntime.quantization.quantize_static` | `modelopt.onnx.quantization.quantize` |
| 量化模式 | INT8（QDQ） | modelopt 可导出 INT8 / FP8 / INT4（`quantize_mode=`）；当前 IxRT 显式量化路线只按 INT8 QDQ 使用 |
| 标定方法 | MinMax / Entropy / Percentile | INT8/FP8：`entropy`(默认)/`max`；INT4：`awq_clip`(默认)/`rtn_dq` |
| 标定数据 | `CalibrationDataReader.get_next()` | 官方主推 **`.npy`/`.npz`**（dict：key=输入名）；也接受 `calibration_data_reader`（继承 ORT reader，但当前 fork 会额外调用 `get_first()`） |
| QDQ 规则 | 通用 QDQ，需手动设对称等 TRT 约束 | 默认按 **TensorRT 规则**插 Q/DQ |
| 非量化部分 dtype | 保持 fp32 | **默认 `high_precision_dtype="fp16"` 会把全图非量化张量转 fp16**，要 fp32 须显式传 `"fp32"` |

INT4（`awq_clip`）和 FP8 是 modelopt 相对 ORT 路线的主要增量，但不要据此推导 IxRT 当前支持 FP8 / INT4 通用精度；IxRT 端可用精度限制见 [`unsupported-interfaces.md`](unsupported-interfaces.md)。

## 最小用法（官方 `.npy` 形式）

```python
import modelopt.onnx.quantization as moq
import numpy as np

# 单输入：直接存 ndarray；多输入：存 dict{输入名: ndarray} 的 .npz
calib = np.load("calib_data.npy")          # shape = [N, C, H, W]，N 张校准样本

moq.quantize(
    onnx_path="model.onnx",
    calibration_data=calib,
    output_path="model_qdq.onnx",
    quantize_mode="int8",                  # int8 / fp8 / int4
    calibration_method="entropy",          # int8: entropy/max
)
```

## 本仓的用法（`export_modeopt.py`，复用 reader + 对齐 fp32）

为与 `export.py` 严格对照（同一张 onnxslim+onnxsim 截断图、只换量化器），脚本没用 `.npy` 而是走 `calibration_data_reader`，并强制非量化部分保持 fp32。注意当前 fork 会在 MatMul/GEMV pattern 分析阶段调用 `reader.get_first()`，自定义 reader 不能只实现 ORT 原生的 `get_next()`：

```python
from onnxruntime.quantization import CalibrationDataReader
from modelopt.onnx.quantization import quantize

class Reader(CalibrationDataReader):
    def get_next(self):
        ...
    def get_first(self):
        ...  # 返回第一组 {input_name: ndarray}，不推进 get_next() 迭代器

quantize(
    onnx_path=cut_path,
    output_path=qdq_path,
    quantize_mode="int8",
    calibration_method="entropy",
    calibration_data_reader=Reader(),       # 复用 ORT 的 CalibrationDataReader，预处理与 eval 一致
    calibration_eps=["cuda:0", "cpu"],      # 标定时跑 ONNX 推理的后端优先级；Corex 侧用 cuda/cpu
    op_types_to_quantize=["MatMul", "Conv", "Gemm"],
    high_precision_dtype="fp32",            # 关键：否则默认 fp16 会改变非量化张量
)
```

要点：
- **`calibration_eps`**：ModelOpt 标定阶段会真跑 ONNX 推理来统计激活范围。Corex 侧用 `cuda:0`，必要时回退 `cpu`；`"trt"` 不是 IxRT 后端，不要把它当成 `ixrtexec` 或 IxRT binding。
- **`high_precision_dtype="fp32"`**：默认 `"fp16"` 会把全图非量化张量转 fp16，和 ORT 路线的 fp32+INT8 QDQ 不一致；两后端对比精度时必须显式 fp32。
- 输出文件名 `yolov8n_nodecode_qdq_modelopt_dyn.onnx` 与 ORT 路线的 `..._qdq_dyn.onnx` 区分，便于并排对比两后端的 mAP / cosine。

## 交给 IxRT

与 ORT 路线完全相同——产物都是带 Q/DQ 的 ONNX：

```bash
ixrtexec --onnx=model_qdq_modelopt.onnx --save_engine=model.engine --verify_acc
```

IxRT 检测到 Q/DQ 即走显式 INT8，**不要再叠加 `set_dynamic_range`**（详见 [`ixrt-int8-quant.md`](ixrt-int8-quant.md)、[`ixrtexec.md`](ixrtexec.md) 精度验证节）。

> modelopt 的 ONNX 量化只生成 QDQ 图，**本身不产生加速**；真正的 INT8 性能要靠后端（NV 侧 `trtexec`、Corex 侧 `ixrtexec`）把 QDQ 图编译成引擎。
