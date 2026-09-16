# yolo-decode-nms-plugin — YOLOv8 裸头 decode + NMS 单一融合 TensorRT/IxRT 插件

## 目的

把 `iluvatar-ixrt/scripts/onnx-qdq-export` 导出的 **YOLOv8 "裸头" nodecode onnx**(只到检测头 6 个
leaf conv，不带 DFL decode / NMS）在 TensorRT/IxRT 上跑通成端到端检测：用**一个**自定义插件
`YoloDecodeNMS` 完成 **DFL 解码 + NMS**，输出标准 4 张量：
`num_dets / det_boxes / det_scores / det_classes`。

```
nodecode onnx(6 裸头张量) → [YoloDecodeNMS 插件] → num_dets / det_boxes / det_scores / det_classes
                                ├── 自写：YOLOv8 DFL decode + EfficientNMS（含 batch 支持）
```

## 关键设计

- **DFL decode 自写**：每 anchor 解 xyxy；每 class 写 sigmoid 分数到
  `boxes[N,A,4]` + `scores[N,A,C]`，布局对齐 EfficientNMS（`scores[anchor*C+c]`，`box_coding=0`，
  `score_activation=0`）。支持动态 batch。
- engine 用 optimization profile：`images` min`[1,3,640,640]` / opt`[16,...]` / max`[32,...]`。
- 插件 decode kernel 按 `batch*H*W` 起 grid，EfficientNMS 本身支持 batch（`param.batchSize`）。

## 注意

`EfficientNMS` 的 NMS kernel 用固定大小 `threadState[NMS_TILES]`（`NMS_TILES=5`）。`numSelectedBoxes`
默认 **4096**，而 `tileSize = numSelectedBoxes/NMS_TILES = 819`，于是 `numTiles = ceil(4096/819) = 6 > 5`
→ **越界访问 `threadState[5]`**。只在 dense 路径（`score_threshold < 0.007`，候选 ≥4096，评 mAP 时的
低阈值）触发，输出全 0 / 垃圾；高阈值（部署 conf=0.25）走 sparse 路径不触发，所以容易漏。

**修复**：插件里把 `numSelectedBoxes` 设成 `NMS_TILES` 的倍数 **4095**（`makeParam`）。



## 编译

**TensorRT**：
```bash
bash build_nv.sh
```

**IxRT**：
```bash
bash build.sh
```



### RT推理+yoloDecodeNMSPlugin

`eval.py` — DetectionValidator + torch CUDA buffer；动态 batch（随机 1~32），fp16 / int8。

```bash
PLUGIN_SO=./libyolo_decode_nms.so \
ONNX_DIR=../onnx-qdq-export \
YOLO_PT=../onnx-qdq-export/yolov8n.pt \
COCO_YAML=/mnt/share/2/dataset/coco_val/coco_val.yaml \
python3 eval.py all
```

| 模式 | mAP50-95（TensorRT） | mAP50（TensorRT） | mAP50-95（IxRT） | mAP50（IxRT） |
|---|---|---|---|---|
| FP16 | 0.3678 | 0.5184 | 0.3678 | 0.5184 |
| INT8 | 0.3567 | 0.5063 | 0.3507 | 0.5003 |

### 推理速度对比

**TensorRT**（`trtexec`，新版用 `--staticPlugins`）：

```bash
# fp16 batch=1
trtexec --onnx=../onnx-qdq-export/yolov8n_nodecode_dyn.onnx \
  --staticPlugins=./libyolo_decode_nms.so \
  --minShapes=images:1x3x640x640 --optShapes=images:16x3x640x640 --maxShapes=images:32x3x640x640 \
  --shapes=images:1x3x640x640 --fp16 --warmUp=50 --iterations=200

# fp16 batch=16
trtexec --onnx=../onnx-qdq-export/yolov8n_nodecode_dyn.onnx \
  --staticPlugins=./libyolo_decode_nms.so \
  --minShapes=images:1x3x640x640 --optShapes=images:16x3x640x640 --maxShapes=images:32x3x640x640 \
  --fp16 --warmUp=50 --iterations=200

# int8 batch=1
trtexec --onnx=../onnx-qdq-export/yolov8n_nodecode_qdq_dyn.onnx \
  --staticPlugins=./libyolo_decode_nms.so \
  --minShapes=images:1x3x640x640 --optShapes=images:16x3x640x640 --maxShapes=images:32x3x640x640 \
  --shapes=images:1x3x640x640 --int8 --warmUp=50 --iterations=200

# int8 batch=16
trtexec --onnx=../onnx-qdq-export/yolov8n_nodecode_qdq_dyn.onnx \
  --staticPlugins=./libyolo_decode_nms.so \
  --minShapes=images:1x3x640x640 --optShapes=images:16x3x640x640 --maxShapes=images:32x3x640x640 \
  --int8 --warmUp=50 --iterations=200
```

**IxRT**（`ixrtexec`，动态 profile min1/opt16/max32）：

```bash
# fp16 batch=1
ixrtexec --onnx ../onnx-qdq-export/yolov8n_nodecode_dyn.onnx \
  --plugins ./libyolo_decode_nms.so \
  --min_shape images:1x3x640x640 --opt_shape images:16x3x640x640 --max_shape images:32x3x640x640 \
  --precision fp16 --iterations 200 --warmUp 50

# fp16 batch=16
ixrtexec --onnx ../onnx-qdq-export/yolov8n_nodecode_dyn.onnx \
  --plugins ./libyolo_decode_nms.so \
  --min_shape images:1x3x640x640 --opt_shape images:16x3x640x640 --max_shape images:32x3x640x640 \
  --precision fp16 --shapes images:16x3x640x640 --iterations 200 --warmUp 50

# int8 batch=1
ixrtexec --onnx ../onnx-qdq-export/yolov8n_nodecode_qdq_dyn.onnx \
  --plugins ./libyolo_decode_nms.so \
  --min_shape images:1x3x640x640 --opt_shape images:16x3x640x640 --max_shape images:32x3x640x640 \
  --precision int8 --iterations 200 --warmUp 50

# int8 batch=16
ixrtexec --onnx ../onnx-qdq-export/yolov8n_nodecode_qdq_dyn.onnx \
  --plugins ./libyolo_decode_nms.so \
  --min_shape images:1x3x640x640 --opt_shape images:16x3x640x640 --max_shape images:32x3x640x640 \
  --precision int8 --shapes images:16x3x640x640 --iterations 200 --warmUp 50
```

**实测结果**

| 精度 | batch | TensorRT 吞吐 (img/s) | IxRT 吞吐 (img/s) |
|---|---|---|---|
| FP16 | 1 | 901 | 697 |
| FP16 | 16 | 2160 | 1645 |
| INT8 | 1 | 935 | 540 |
| INT8 | 16 | 2477 | 1167 |
