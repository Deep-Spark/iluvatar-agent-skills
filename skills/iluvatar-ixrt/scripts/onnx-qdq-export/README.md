# onnx-qdq-export — 导出"裸头"YOLOv8 QDQ ONNX（decode+NMS 不导出）

供 IxRT 显式量化消费。IxRT 检测图里的 `QuantizeLinear`/`DequantizeLinear`（Q/DQ）即走 INT8
（见 [`../../references/ixrt-int8-quant.md`](../../references/ixrt-int8-quant.md)、
[`../../references/onnx-qdq-export.md`](../../references/onnx-qdq-export.md)）。

## 方案：decode + NMS 都不导出

DFL 解码、box/score 拼接、NMS 全部不进图，交给 IxRT 端自写插件。

```
yolov8n.pt --ultralytics(nms=False,dynamic=True)--> 临时 yolov8n.onnx
   → onnx.utils.extract_model 截到检测头 6 个 leaf conv 输出（cv2.N.2 / cv3.N.2）
   → INT8 QDQ 量化（两种后端，见下）
   → 仅保留 yolov8n_nodecode_dyn.onnx + *_qdq_*.onnx
```

### 两种量化后端

截断（`extract_model`）这一步两条线完全一致，仅第 3 步量化器不同：

| 脚本 | 量化器 | 产物 | 特点 |
|---|---|---|---|
| `export.py` | `onnxruntime.quantization.quantize_static`| `yolov8n_nodecode_qdq_dyn.onnx` | 需一串 TRT 兼容 `extra_options` |
| `export_modeopt.py` | `modelopt.onnx.quantization.quantize` | `yolov8n_nodecode_qdq_modelopt_dyn.onnx` | 默认对称 per-channel，本就面向 TRT/IxRT，无需额外 extra_options |

`export_modeopt.py` 用 `high_precision_dtype="fp32"` 保非量化部分 fp32（对齐 `export.py`），`quantize_mode="int8"` + `calibration_method="entropy"`，`calibration_eps=cuda:0,cpu`（标定阶段用 CUDA/CPU 跑 ONNX，不把 `trt` 当 IxRT 后端），只量化 `MatMul/Conv/Gemm`；校准 reader 复用同一套 ultralytics `LetterBox` 预处理，并为 ModelOpt 补充 `get_first()`。

## 产物

仅 **动态 batch**：

| 文件 | 说明 |
|---|---|
| `yolov8n_nodecode_dyn.onnx` | 裸头 fp32/fp16，input `[batch,3,height,width]` |
| `yolov8n_nodecode_qdq_dyn.onnx` | 裸头 int8 QDQ（ORT 量化），batch 维动态 |
| `yolov8n_nodecode_qdq_modelopt_dyn.onnx` | 裸头 int8 QDQ（Model Optimizer 量化），batch 维动态 |

截断后的 6 个裸头输出（decode 前，`batch=1, 640×640` 示例）：

| 节点 | shape | 含义 | stride |
|---|---|---|---|
| `cv2.0/cv2.0.2/Conv` | `[1, 64, 80, 80]` | box，64 DFL bins | 8 |
| `cv3.0/cv3.0.2/Conv` | `[1, 80, 80, 80]` | 80 类 score | 8 |
| `cv2.1/cv2.1.2/Conv` | `[1, 64, 40, 40]` | box，64 DFL bins | 16 |
| `cv3.1/cv3.1.2/Conv` | `[1, 80, 40, 40]` | 80 类 score | 16 |
| `cv2.2/cv2.2.2/Conv` | `[1, 64, 20, 20]` | box，64 DFL bins | 32 |
| `cv3.2/cv3.2.2/Conv` | `[1, 80, 20, 20]` | 80 类 score | 32 |

量化后 `yolov8n_nodecode_qdq_dyn.onnx` 含 Q/DQ 节点与 int8 权重；`export.py` 结束时会打印
`QuantizeLinear` / `DequantizeLinear` / int8 权重计数（随 ORT 版本或校准集可能变化）。

## 交给 IxRT

- 这 6 个张量由 IxRT 端自写的 decode + NMS 插件消费，见 [`../yolo-decode-nms-plugin/`](../yolo-decode-nms-plugin/)。

## 运行

环境需已装好 `ultralytics`、`onnx`、`onnxruntime-gpu`、`onnxslim`、`onnxsim`（截断后清图）；`export_modeopt.py` 另需 Model Optimizer。

```bash
cd iluvatar-ixrt/scripts/onnx-qdq-export
```

### 导出

裸头 fp32 + QDQ int8（约 2–3 分钟，校准默认从 1000 张里抽 `CALIB_N=100` 张）：

```bash
python3 export.py            # ORT quantize_static 后端
python3 export_modeopt.py    # Model Optimizer 后端
```

### 精度评测

DetectionValidator + ORT CUDA，COCO val2017 5000 张；

```bash
export LD_LIBRARY_PATH=/usr/local/corex/lib64:$LD_LIBRARY_PATH
COCO_YAML=/mnt/share/2/dataset/coco_val/coco_val.yaml python3 eval.py
```

**实测结果**（COCO val2017，5000 图，`conf=0.001`，`iou=0.7`）：

| 模式 | mAP50-95 | mAP50 |
|---|---|---|
| baseline `yolov8n.pt` GPU | 0.3677 | 0.5183 |
| fp32 nodecode ORT-GPU | 0.3677 | 0.5183 |
| int8 QDQ nodecode（ORT 量化） | 0.3456 | 0.4947 |
| int8 QDQ nodecode（Model Optimizer 量化） | 0.3598 | 0.5112 |

**两后端对比**：Model Optimizer 的对称 per-channel + entropy 标定明显优于 ORT `quantize_static`，
mAP50-95 掉点从 -0.0221 缩到 -0.0079（几乎追平 fp32），mAP50 同样更好。`eval.py` 一次评 baseline `.pt`
与上述三个 onnx。

示例数据集根目录：`/mnt/share/2/dataset/coco_val/`（yaml、val 图片、`labels/` YOLO txt 均在同一目录下）。

## 说明

- **INT8 校准图**（`export.py` 默认 Entropy 标定，从 1000 张里随机抽 `CALIB_N=100` 张）：
  - 下载 `coco_calib.zip`（约 149 MB，1000 张）：
    - [Google Drive — tensorrtx-int8calib-data](https://drive.google.com/drive/folders/1s7jE9DtOngZMzJC1uL307J2MiaGwdRSI)
    - [百度网盘](https://pan.baidu.com/s/1GOm_-JobpyLMAqZWCDUhKg)（提取码：`a9wh`）
  - 解压到 `coco_calib/` 目录（默认路径 `/mnt/share/2/dataset/int8calib-data/coco_calib/`）
- **COCO val 评测集**（5000 张）：
  - `coco.yaml` https://github.com/ultralytics/ultralytics/blob/main/ultralytics/cfg/datasets/coco.yaml
  - val 图片 http://images.cocodataset.org/zips/val2017.zip
  - YOLO 标注 https://github.com/ultralytics/assets/releases/download/v0.0.0/coco2017labels.zip
