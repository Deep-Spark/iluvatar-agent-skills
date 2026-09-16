# ixrt-layer-profiling — 用 ixrtexec 逐层 profiler 定位「IxRT 上 int8 反而更慢」

裸头 YOLOv8n（decode/NMS 不进图，由 [`../onnx-qdq-export/`](../onnx-qdq-export/) 导出）三个 ONNX
在 **TensorRT** 与 **IxRT** 上的推理速度对比，以及为什么同一组 QDQ 模型在两平台上**快慢排序完全相反**。

| ONNX | 量化 |
|---|---|
| `yolov8n_nodecode_dyn.onnx` | 无（fp16 推理） |
| `yolov8n_nodecode_qdq_dyn.onnx` | ORT `quantize_static`（QDQ 插满，含 SiLU） |
| `yolov8n_nodecode_qdq_modelopt_dyn.onnx` | Model Optimizer（融合感知，删 SiLU 边界 QDQ） |

## 1. trtexec 推理速度（NVIDIA A10）

输入 `images [batch,3,h,w]`，三轴动态，`--shapes images:16x3x640x640`。

| 模型 | 精度 | Throughput (qps) | 图像吞吐 (img/s) | 每张 (ms) |
|---|---|---|---|---|
| fp16 裸头 | fp16 | 134.8 | ~2157 | ~0.46 |
| ORT-QDQ | int8+fp16 | 162.0 | ~2592 | ~0.35 |
| modelopt-QDQ | int8+fp16 | **181.4** | **~2903** | **~0.27** |

INT8 相对 fp16：ORT-QDQ **-25%**，modelopt-QDQ **-41%**。
**TRT 上：modelopt > ORT > fp16（越量化越快）。**

```bash
trtexec --onnx=yolov8n_nodecode_qdq_modelopt_dyn.onnx --int8 --fp16 \
    --minShapes=images:1x3x640x640 --optShapes=images:16x3x640x640 --maxShapes=images:32x3x640x640 \
    --shapes=images:16x3x640x640
```

## 2. ixrtexec 推理速度（Iluvatar MR-V100）

`ixrtexec` 默认只打印 `fps`（img/s）与 `Throughput`（batch=16 的 qps），不细分 compute/H2D/D2H。

| 模型 | 精度 | Throughput (qps) | 图像吞吐 fps (img/s) | 每张 (ms) |
|---|---|---|---|---|
| fp16 裸头 | fp16 | **105.18** | **1682.91** | ~0.59 |
| ORT-QDQ | int8+fp16 | 84.06 | 1344.96 | ~0.74 |
| modelopt-QDQ | int8+fp16 | 58.86 | 941.81 | ~1.06 |

```bash
ixrtexec --onnx=yolov8n_nodecode_qdq_modelopt_dyn.onnx --precision int8 fp16 \
    --min_shape images:1x3x640x640 --opt_shape images:16x3x640x640 --max_shape images:32x3x640x640 \
    --shapes images:16x3x640x640
```

### ⚠️ 排序与 TensorRT 完全相反

| 排序（快→慢） | TensorRT (A10) | IxRT (MR-V100) |
|---|---|---|
| 1 | modelopt-QDQ（-41%） | **fp16**（1682.91 fps） |
| 2 | ORT-QDQ（-25%） | ORT-QDQ（1344.96，比 fp16 慢 20%） |
| 3 | fp16（baseline） | **modelopt-QDQ**（941.81，比 fp16 慢 44%） |

TRT 上 int8 越量化越快、modelopt 最快；**IxRT 上两个 int8 都比 fp16 慢，且 TRT 最快的 modelopt 在 IxRT 最慢。**

## 3. 逐层 profiler——耗时去向（`ixrtexec --run_profiler`）

`run.sh` 对三个模型跑 `--run_profiler --export_profiler *.csv`，再用 `profile_layers.py` 按算子类别
聚合 `Avg. Time(ms)`（batch=16）。三方对比：

| 类别 | fp16 | ORT-QDQ | modelopt-QDQ |
|---|---|---|---|
| **总计** | 105.63 ms / 222 层 | 138.39 ms / 450 层 | **196.96 ms / 528 层** |
| quantize/dequant | — | 57.69 ms ×181 | **63.83 ms ×232** |
| **conv** | 51.50 ms ×130 | **33.87 ms ×138** | **51.62 ms ×130** |
| reformat | 36.25 ms ×23 | 23.99 ms ×50 | **63.65 ms ×97** |
| concat/split/resize/maxpool/add/其余 | 17.88 ms | 22.85 ms | 17.87 ms |

总时之比 105.63 : 138.39 : 196.96，与无 profiler 吞吐排序一致。

## 4. 根因：IxRT 的 int8 conv 要求"输出端有 Q"，modelopt 恰好把它删了

**看 conv 那一行**就够了：

- fp16 conv **51.50 ms**，modelopt conv **51.62 ms** → **几乎相等**，说明 **modelopt 的卷积绝大多数按 fp16 在跑，没吃到 int8**。
- ORT conv **33.87 ms** → 明显更快，走了 int8 快路径。

为什么 modelopt 的 conv 退回 fp16？关键**不是** IxRT 不会融合 conv+SiLU——它**会**（SiLU 在可融合激活
列表里，RELU/SIGMOID/SILU/HARD_SWISH/MISH 等都在）。真正的规则是 **IxRT 认定一个 conv 走 int8，要求
凑齐一个完整的 `DQ → Conv → SiLU → Q` 夹心**：

| 条件 | 含义 |
|---|---|
| conv 输入来自 DQ | 输入是 int8 |
| conv 权重来自 DQ | 权重是 int8 |
| **激活（SiLU）输出被 Q 消费** | **输出端必须有 Q 收口** |

三者**缺一个**，IxRT 就不给该 conv 请求 int8 精度，conv 整个退回 fp16 kernel；边界上还得反复插 reformat
（97 vs 50）。

- **modelopt**：为给 TensorRT 融合，它的 `remove_partial_input_qdq` 把 `conv→SiLU` 边界上的 Q/DQ 删掉
  （TRT 的 int8 conv 可以直接吐 fp16、不需要输出 Q）。这恰好删掉了 IxRT 所必需的**输出端 Q** → 条件不齐 →
  conv 跑 fp16。TRT 上的 -41% 优化，到 IxRT 反成催命符。
- **ORT**：把 SiLU（Sigmoid+Mul）也显式量化了，SiLU 输出端**有 Q** → `DQ→Conv→SiLU→Q` 夹心齐活 →
  conv+SiLU 融成一个 int8 kernel（34 ms）。代价是 quantize 层多，但 conv 全程 int8。

> **一句话**：IxRT 的 int8 conv 必须被 Q "收口"（`DQ→Conv→Act→Q`）。modelopt 为 TRT 融合删掉收口 Q，
> 在 IxRT 上等于把 conv 踢出 int8；ORT"连 SiLU 一起量化"反而满足了这个收口要求。**照搬 TensorRT
> 「int8/modelopt 必更快」的结论到 IxRT 是错的**——必须按目标环境的 `ixrtexec` 实测决定是否上 int8。

`ixrtexec` 启动也提示「建议先 onnxsim 简化再转」；实测在「截断后、量化前」加 onnxslim+onnxsim，
源图 Q/DQ 计数与结构不变，speed 也追不上——简化工具不删合法 QDQ，救不了这里。

## 5. ixsys 实锤：conv kernel 的 dtype

profiler 的 conv 耗时≈fp16 已是强证据，用 **`ixsys`**（系统级 trace，抓 GPU kernel 时间线带全名、
无需重放）直接看 kernel 模板里的 dtype，把"modelopt conv 跑 fp16"钉死：

```bash
ixsys -t cuda_kernel -o /tmp/tr \
  ixrtexec --onnx=../onnx-qdq-export/yolov8n_nodecode_qdq_modelopt_dyn.onnx --precision int8 fp16 \
  --min_shape images:1x3x640x640 --opt_shape images:1x3x640x640 --max_shape images:1x3x640x640 \
  --shapes images:1x3x640x640 --iterations 1 --warmUp 0
# 产物是 sqlite，直接 strings 看 kernel 名即可
strings /tmp/tr | grep -aoE "implConv[^(]*>"
```

**卷积 kernel 模板参数（`<…, A, B, acc>`）里的 A/B 就是输入/权重 dtype：**

| | 卷积 kernel | dtype 模板参数 | 结论 |
|---|---|---|---|
| **ORT-QDQ** | `implConvolutionTcuHWReuseKernel` / `implConvolutionTcuBypassSlbKernel` | `…, int, signed char, signed char, float` | **int8 输入+int8 权重+int32 累加 = 真 INT8 卷积** |
| **modelopt-QDQ** | `implConv2DFp16Algo1…KernelTemplate` / `implConvolutionTcuGemmKernel` | `…, __half, __half, float` | **fp16 卷积**（含 `signed char` 的 conv kernel 数 = **0**） |

modelopt trace 里的 `signed char` 只出现在 `convert_dtype<signed char, __half>` /
`convert_dtype_to_int8_pack4` 这类 **quantize/reformat 转换 kernel**，卷积运算本身没有 int8——
与 profiler「modelopt conv 51.62ms ≈ fp16 51.50ms，ORT conv 33.87ms」完全吻合：IxRT 没把 modelopt 的 conv 选成
int8 kernel，量化只剩 convert/reformat 的纯开销。

> `ixrtexec --dump_graph` 走 `IXRT_DEBUG_TOOL` 当前版本会 core，故看 kernel 名以 ixsys 为准。
> 参见 skill `iluvatar-profiling-tools`。

## 运行

```bash
bash run.sh
```

`profile_layers.py` 也可单独喂任意 ixrtexec profiler CSV：

```bash
python3 profile_layers.py /tmp/ixrt-layer-profiling/*.csv
```
