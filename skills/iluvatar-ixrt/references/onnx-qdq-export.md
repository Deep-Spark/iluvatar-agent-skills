# 导出 ONNX QDQ 量化模型（喂给 IxRT 显式量化）

IxRT 的显式量化路径（见 [`ixrt-int8-quant.md`](ixrt-int8-quant.md)）会在图里检测 `QuantizeLinear`/`DequantizeLinear`（Q/DQ）节点并自动走 INT8——这些 Q/DQ 节点除了用 ixrt API 手插，更常见的来源是**直接导出一个带 Q/DQ 的 ONNX 模型**，再用 `ixrtexec --onnx=model_qdq.onnx` 或 ixrt Python 解析器加载。本文是用 ONNX Runtime 量化工具离线生成这个 QDQ ONNX 的流程。参考：<https://onnxruntime.ai/docs/performance/model-optimizations/quantization.html>

## QDQ 格式

**QDQ**（`QuantFormat.QDQ`）在原算子之间插入 `DequantizeLinear(QuantizeLinear(tensor))` 对，scale/zero-point 作为常量带在 Q/DQ 节点上，跨框架通用——**IxRT 走这一种**，导出时务必选它。

## 静态量化（CNN 首选）

CNN 用静态量化（激活的 scale 离线由校准集标定，写死进图）；RNN / transformer 类官方建议动态量化，但 IxRT 走的是带 Q/DQ 常量 scale 的静态图。

### 1. 预处理（必做）

量化前先跑 `quant_pre_process`（onnxruntime 上游函数，与平台无关）做形状推断 + 图优化，否则部分节点缺 shape 会导致量化失败或精度异常。可跑实现见本目录 `export.py` 的 `pre_process()`（截断 → 预处理 → 量化 三步串起）：

```python
from onnxruntime.quantization.shape_inference import quant_pre_process

quant_pre_process(
    input_model_path="model.onnx",
    output_model_path="model_pre.onnx",
    skip_symbolic_shape=False,   # transformer 模型保留符号形状推断；纯 CNN 静态 shape 可设 True
    skip_optimization=False,
    skip_onnx_shape=False,
    auto_merge=True,             # 符号推断遇维度冲突时自动合并，避免直接报错
)
```

等价 CLI：`python -m onnxruntime.quantization.preprocess --input model.onnx --output model_pre.onnx`。

三步可选：符号形状推断（transformer 适用）、模型优化（图改写）、ONNX 形状推断。**建议优化放在预处理阶段做，量化阶段不再二次优化**，便于逐层对精度时定位问题。官方文档把这一步称作 **Pre-processing**（见上方链接的 *Pre-processing* 一节），`quant_pre_process` 即其背后的 Python 函数。

> 注：若输入图已用 `onnxslim` + `onnxsim` 清理过（如 `export.py` 的 `cut_nodecode`），形状大多已齐全，预处理后变化很小；`quant_pre_process` 的价值主要体现在**未清理过的原始导出图**上。两条路线（onnxslim/onnxsim vs quant_pre_process）目的重叠，按图的干净程度二选一即可。

### 2. 提供校准数据 `CalibrationDataReader`

静态量化要喂真实数据统计激活范围。实现 `get_next()` 逐 batch 返回 `{input_name: ndarray}`，喂完返回 `None`：

```python
import numpy as np
from onnxruntime.quantization import CalibrationDataReader

class MyReader(CalibrationDataReader):
    def __init__(self, calib_samples, input_name):
        self.it = iter([{input_name: s} for s in calib_samples])
    def get_next(self):
        return next(self.it, None)
```

### 3. `quantize_static` 导出 QDQ

```python
from onnxruntime.quantization import quantize_static, QuantFormat, QuantType, CalibrationMethod

quantize_static(
    model_input="model_pre.onnx",
    model_output="model_qdq.onnx",
    calibration_data_reader=MyReader(calib_samples, "input"),
    quant_format=QuantFormat.QDQ,          # 关键：QDQ，不要 QOperator
    per_channel=True,                       # 权重逐通道，conv/matmul 精度更好
    activation_type=QuantType.QInt8,        # 激活 int8（对称，IxRT 友好）
    weight_type=QuantType.QInt8,            # 权重 int8
    calibrate_method=CalibrationMethod.MinMax,  # MinMax / Entropy / Percentile
)
```

校准方法三选一：`MinMax`（最快、范围最宽）、`Entropy`（KL 散度，激活分布长尾时更稳）、`Percentile`（截断离群值）。掉精度时优先从 MinMax 换 Entropy/Percentile 试。

可跑示例（YOLOv8 裸头 QDQ，需校准图 + COCO val）：[`scripts/onnx-qdq-export/`](../scripts/onnx-qdq-export/)，
`python3 export.py` 导出；GPU/CPU 评测见同目录 [`README.md`](../scripts/onnx-qdq-export/README.md)。

## 交给 IxRT

```bash
# 直接用 ixrtexec 构建显式 Q/DQ 引擎并验精度（无需叠加 set_dynamic_range）
ixrtexec --onnx=model_qdq.onnx --save_engine=model_qdq.engine --verify_acc
```

要点：
- **不要再叠加隐式量化的 `set_dynamic_range`**——Q/DQ 已带 scale，混用会冲突。
- IxRT 检测到 Q/DQ 即走显式 INT8 往返；可用 `ixrtexec --verify_acc` 和逐层 profiler 验证精度与耗时（详见 [`ixrtexec.md`](ixrtexec.md)）。
- 与「不支持在线 calibrator」不冲突：标定发生在**导出 ONNX 这一侧**（ORT 工具完成），IxRT 只消费已带 Q/DQ 的图，不需要 `IInt8Calibrator`。
