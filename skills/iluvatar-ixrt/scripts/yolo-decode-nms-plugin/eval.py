#!/usr/bin/env python3
# IxRT YoloDecodeNMS 插件评测；非 IxRT 部分走 ultralytics DetectionValidator 全链路。
# 用法：
#   eval.py [fp16|int8|all]           # 动态 ONNX + 随机 batch(1~32)
#   eval.py --dyn ...                   # 同默认（保留兼容）
#
# ultralytics 来源索引（非 IxRT 部分）：
#   配置/入口     ultralytics/cfg/__init__.py :: get_cfg, DEFAULT_CFG
#   数据集        ultralytics/data/utils.py :: check_det_dataset
#                 ultralytics/data/dataset.py :: YOLODataset.build_transforms (val: LetterBox+Format)
#                 ultralytics/data/augment.py :: LetterBox, Format
#   验证主循环    ultralytics/engine/validator.py :: BaseValidator.__call__
#   检测验证器    ultralytics/models/yolo/detect/val.py :: DetectionValidator
#                 (preprocess, postprocess, _prepare_batch, update_metrics, get_stats)
#   模型后端      ultralytics/nn/autobackend.py :: AutoBackend (nn.Module → format=pt)
#                 ultralytics/nn/backends/pytorch.py :: PyTorchBackend
#   NMS/end2end   ultralytics/utils/nms.py :: non_max_suppression (end2end 分支)
#   匹配          ultralytics/engine/validator.py :: BaseValidator.match_predictions
#   mAP           ultralytics/utils/metrics.py :: DetMetrics.process, ap_per_class
import ctypes
import os
import random
import sys

import numpy as np
import tensorrt as trt
import torch
import torch.nn as nn
from ultralytics.cfg import DEFAULT_CFG, get_cfg
from ultralytics.data.dataset import YOLODataset
from ultralytics.data.utils import check_det_dataset
from ultralytics.models.yolo.detect import DetectionValidator

_HERE = os.path.dirname(os.path.abspath(__file__))
PLUGIN_SO = os.environ.get("PLUGIN_SO", os.path.join(_HERE, "libyolo_decode_nms.so"))
ONNX_DIR = os.environ.get("ONNX_DIR", os.path.join(_HERE, "..", "onnx-qdq-export"))
YOLO_PT = os.environ.get("YOLO_PT", os.path.join(ONNX_DIR, "yolov8n.pt"))
HEAD = [
    "/model.22/cv2.0/cv2.0.2/Conv_output_0", "/model.22/cv3.0/cv3.0.2/Conv_output_0",
    "/model.22/cv2.1/cv2.1.2/Conv_output_0", "/model.22/cv3.1/cv3.1.2/Conv_output_0",
    "/model.22/cv2.2/cv2.2.2/Conv_output_0", "/model.22/cv3.2/cv3.2.2/Conv_output_0",
]
NETSZ, MAXDET = 640, 300
CONF, IOU = 0.001, 0.7
BMIN, BOPT, BMAX = 1, 16, 32
OUT_NAMES = ["num_dets", "det_boxes", "det_scores", "det_classes"]
ONNX = {"fp16": "yolov8n_nodecode_dyn.onnx", "int8": "yolov8n_nodecode_qdq_dyn.onnx"}
DATA = os.environ.get("COCO_YAML", "/root/skill/datasets/coco8/coco8.yaml")
DYN_SEED = int(os.environ.get("DYN_SEED", "0"))
LOG = trt.Logger(trt.Logger.ERROR)
ctypes.CDLL(PLUGIN_SO)
trt.init_libnvinfer_plugins(LOG, "")


# ---------------------------------------------------------------------------
# [IxRT] 以下仅 TensorRT/IxRT 插件路径，ultralytics 无对应实现
# ---------------------------------------------------------------------------

def build_engine(mode):
    if mode not in ONNX:
        raise ValueError(f"unsupported mode {mode!r}, want fp16 or int8")
    builder = trt.Builder(LOG)
    net = builder.create_network(0)
    parser = trt.OnnxParser(net, LOG)
    with open(os.path.join(ONNX_DIR, ONNX[mode]), "rb") as f:
        assert parser.parse(f.read())
    obn = {net.get_output(i).name: net.get_output(i) for i in range(net.num_outputs)}
    ins = [obn[n] for n in HEAD]
    for t in ins:
        net.unmark_output(t)
    cr = trt.get_plugin_registry().get_plugin_creator("YoloDecodeNMS", "1", "")
    fc = trt.PluginFieldCollection([
        trt.PluginField("score_threshold", np.array([CONF], np.float32), trt.PluginFieldType.FLOAT32),
        trt.PluginField("iou_threshold", np.array([IOU], np.float32), trt.PluginFieldType.FLOAT32),
        trt.PluginField("max_det", np.array([MAXDET], np.int32), trt.PluginFieldType.INT32),
        trt.PluginField("net_size", np.array([NETSZ], np.int32), trt.PluginFieldType.INT32),
    ])
    layer = net.add_plugin_v2(ins, cr.create_plugin("y", fc))
    for j, nm in enumerate(OUT_NAMES):
        layer.get_output(j).name = nm
        net.mark_output(layer.get_output(j))
    cfg = builder.create_builder_config()
    cfg.set_flag(trt.BuilderFlag.FP16 if mode == "fp16" else trt.BuilderFlag.INT8)
    prof = builder.create_optimization_profile()
    prof.set_shape("images", (BMIN, 3, NETSZ, NETSZ), (BOPT, 3, NETSZ, NETSZ), (BMAX, 3, NETSZ, NETSZ))
    cfg.add_optimization_profile(prof)
    return builder.build_serialized_network(net, cfg)


def save_engine(mode):
    """构建并序列化 engine 到 yolov8n_{mode}.engine，供 trtexec/ixrtexec --loadEngine 基准测试。"""
    engine_bytes = build_engine(mode)
    out = os.path.join(_HERE, f"yolov8n_{mode}.engine")
    with open(out, "wb") as f:
        f.write(engine_bytes)
    print(f"saved: {out} ({os.path.getsize(out) // 1024} KB)")


class IxRTPluginModel(nn.Module):
    """IxRT 融合插件 wrapper，供 DetectionValidator 调用（模式同 ../onnx-qdq-export/eval.py :: NodecodeModel）。"""

    end2end = True  # [ultralytics] utils/nms.py :: non_max_suppression(end2end=True)

    def __init__(self, mode, names, dynamic=True):
        super().__init__()
        self.names = names
        self.dynamic = dynamic
        self.bmax = BMAX if dynamic else 1
        self.pt = True       # [ultralytics] nn/autobackend.py :: format='pt' when isinstance(model, nn.Module)
        self.fp16 = mode == "fp16"
        self.stride = torch.tensor([8.0, 16.0, 32.0])  # [ultralytics] detect/val.py :: init_metrics
        device = torch.device("cuda")
        ctx = trt.Runtime(LOG).deserialize_cuda_engine(build_engine(mode)).create_execution_context()
        self._ctx = ctx
        b = self.bmax
        self._bufs = {
            "num_dets": torch.empty((b, 1), dtype=torch.int32, device=device),
            "det_boxes": torch.empty((b, MAXDET, 4), dtype=torch.float32, device=device),
            "det_scores": torch.empty((b, MAXDET), dtype=torch.float32, device=device),
            "det_classes": torch.empty((b, MAXDET), dtype=torch.int32, device=device),
        }
        for name, t in self._bufs.items():
            ctx.set_tensor_address(name, int(t.data_ptr()))

    def fuse(self, verbose=True):
        return self  # [ultralytics] nn/backends/pytorch.py :: PyTorchBackend.load_model

    def set_head_attr(self, **kwargs):
        pass  # [ultralytics] engine/validator.py :: end2end 模型可选接口

    def _pack_out(self, im, k):
        cnt = int(self._bufs["num_dets"][k, 0].item())
        if cnt:
            return torch.cat([
                self._bufs["det_boxes"][k, :cnt],
                self._bufs["det_scores"][k, :cnt, None],
                self._bufs["det_classes"][k, :cnt, None].float(),
            ], dim=1)
        return im.new_zeros((0, 6))

    def forward(self, im, *args, **kwargs):
        """[ultralytics] batch['img'] BCHW /255；[IxRT] 输出 [B,N,6] 供 postprocess end2end 分支。"""
        if im.dim() == 3:
            im = im.unsqueeze(0)
        bs = im.shape[0]
        if bs > self.bmax:
            raise ValueError(f"batch {bs} > bmax {self.bmax}")
        im = im.contiguous().float()
        if im.device.type != "cuda":
            im = im.cuda()
        if self.dynamic:
            self._ctx.set_input_shape("images", tuple(im.shape))
        self._ctx.set_tensor_address("images", int(im.data_ptr()))
        stream = torch.cuda.current_stream()
        self._ctx.execute_async_v3(stream.cuda_stream)
        stream.synchronize()
        if self.dynamic:
            out = im.new_zeros((bs, MAXDET, 6))
            for k in range(bs):
                row = self._pack_out(im, k)
                if row.shape[0]:
                    out[k, : row.shape[0]] = row
            return out
        out = self._pack_out(im, 0)
        return out.unsqueeze(0)


# ---------------------------------------------------------------------------
# [ultralytics] DetectionValidator 整条 val 流水线（参考 ../onnx-qdq-export/eval.py :: run）
# ---------------------------------------------------------------------------

def _validator_args():
    args = get_cfg(DEFAULT_CFG)
    args.data = DATA
    args.imgsz = NETSZ
    args.batch = 1
    args.rect = False
    args.device = "cuda"
    args.conf = CONF
    args.iou = IOU
    args.max_det = MAXDET
    args.split = "val"
    args.workers = 0
    args.verbose = False
    return args


def run_val(mode, seed=DYN_SEED):
    """动态 ONNX + 随机 batch(1~32)；仍用 DetectionValidator 的 preprocess/postprocess/metrics。"""
    args = _validator_args()
    from ultralytics import YOLO
    names = YOLO(YOLO_PT).model.names
    model = IxRTPluginModel(mode, names, dynamic=True)
    v = DetectionValidator(args=args)
    v.training = False
    v.device = torch.device("cuda")
    v.data = check_det_dataset(args.data)
    v.stride = model.stride
    v.init_metrics(model)
    v.jdict = []
    dataset = v.build_dataset(v.data.get(args.split), batch=BMAX, mode="val")
    indices = list(range(len(dataset)))
    rng = random.Random(seed)
    rng.shuffle(indices)
    bsizes, pos = [], 0
    while pos < len(indices):
        bs = rng.randint(BMIN, BMAX)
        idxs = indices[pos:pos + bs]
        pos += len(idxs)
        bs = len(idxs)
        bsizes.append(bs)
        batch = YOLODataset.collate_fn([dataset[i] for i in idxs])
        batch = v.preprocess(batch)
        preds = v.postprocess(model(batch["img"]))
        v.update_metrics(preds, batch)
    v.gather_stats()
    v.get_stats()
    v.finalize_metrics()
    print(
        f"  随机 batch 分布: {bsizes[:12]}{'...' if len(bsizes) > 12 else ''} "
        f"共 {len(bsizes)} 批, 范围[{min(bsizes)},{max(bsizes)}]",
        flush=True,
    )
    return float(v.metrics.box.map), float(v.metrics.box.map50)


def parse_argv():
    modes, do_save = [], False
    for arg in sys.argv[1:] or ["fp16"]:
        if arg in ("--dyn", "dyn"):
            continue
        if arg == "--save":
            do_save = True
        elif arg in ONNX or arg == "all":
            modes.append(arg)
        else:
            raise SystemExit(f"unknown arg {arg!r}, want fp16|int8|all [--save]")
    if not modes:
        modes = ["fp16"]
    return modes, do_save


def main():
    modes, do_save = parse_argv()
    if modes == ["all"]:
        modes = ["fp16", "int8"]
    if do_save:
        for mode in modes:
            save_engine(mode)
        return
    for mode in modes:
        m, m50 = run_val(mode)
        print(f"[{mode} plugin dyn-batch] mAP50-95={m:.4f}  mAP50={m50:.4f}", flush=True)


if __name__ == "__main__":
    main()
