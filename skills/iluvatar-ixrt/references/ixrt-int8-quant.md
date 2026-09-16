# IxRT INT8 量化（隐式 dynamic-range / 显式 Q/DQ）

IxRT 支持 INT8 量化，两条互斥路径。显式 Q/DQ 的导出流程见 [`onnx-qdq-export.md`](./onnx-qdq-export.md) / [`onnx-qdq-export-modeopt.md`](./onnx-qdq-export-modeopt.md)，性能定位案例见 [`scripts/ixrt-layer-profiling/`](../scripts/ixrt-layer-profiling/)：

> **前提：只支持对称量化（symmetric）**。`zero_point` 恒为 0，量化区间关于零对称——没有非对称/带零点量化。
> per-tensor（整张量共用一个 `scale`）与 per-channel 都支持；对线性层（Conv/MatMul）的 **per-channel 实际是 per-column**（按权重输出通道/列各算一个 scale）。
> IxRT **不提供在线校准 API**：要么喂带 Q/DQ 的 ONNX，要么 build 后手动 `set_dynamic_range`。离线导出 QDQ ONNX 的两条上游路线见 [`onnx-qdq-export.md`](./onnx-qdq-export.md)（ORT）与 [`onnx-qdq-export-modeopt.md`](./onnx-qdq-export-modeopt.md)（Model Optimizer）。

## 隐式量化（dynamic-range）

`BuilderFlag.INT8` + 对激活张量 `set_dynamic_range(min,max)`，权重 scale 自动算。
按层速度决定是否真用 INT8 kernel（小 conv 可能保留高精度故 cosine=1）。

**平台差异**：较新的 NVIDIA TensorRT 推荐改用显式 Q/DQ；IxRT 当前仍保留 `set_dynamic_range` 路径。

## 显式量化（Q/DQ）

图里插 `add_quantize` / `add_dequantize`（Q/DQ）、自带 scale，**不需开 INT8 flag**，
IxRT 检测到 Q/DQ 即走显式量化。

底层 `NativeQuantize` / `NativeDequantize` / `QuantizeAttribute` 有真实实现；NVIDIA TensorRT 侧也推荐 Q/DQ 作为显式量化路径。

## 不支持 INT8 在线量化（calibrator）

`IInt8Calibrator` / `IInt8EntropyCalibrator` / `IInt8EntropyCalibrator2` / `IInt8MinMaxCalibrator`
只在注释里出现（无真正 `class` 定义），`IBuilderConfig` 也没有 `setInt8Calibrator` →
隐式量化的范围必须自己 `set_dynamic_range` 提供，不能喂数据集自动标定。

## ⚠️ INT8 在 IxRT 上不一定更快

同一组裸头 YOLOv8n QDQ 模型的案例里，IxRT 上两个 int8 版本都比纯 fp16 慢。根因是 **IxRT 的 int8 conv 要求完整的 `DQ→Conv→SiLU→Q` 夹心（输出端必须有 Q 收口）**：modelopt 为 TensorRT 融合删掉了 SiLU 边界的 Q，恰好抽掉这个收口 → conv 退回 fp16 速度；而 ORT「把 SiLU 也显式量化」让 SiLU 输出端有 Q、夹心齐活，conv+SiLU 融成 int8 反而更快。（IxRT 并非不会融合 conv+SiLU，SiLU 就在可融合激活列表里。）逐层 `ixrtexec --run_profiler` 复现 + 分析见 [`scripts/ixrt-layer-profiling/`](../scripts/ixrt-layer-profiling/)。

**结论：别照搬 TRT 的「int8 必更快」，按目标环境的 ixrtexec 实测决定。**
