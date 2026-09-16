#!/usr/bin/env python3
# 用 ultralytics 原生 DetectionValidator 评 "裸头" nodecode onnx —— onnx 不带 decode 头，
# decode 由本脚本在 onnx 外补出（make_anchors + DFL + dist2bbox，与插件同逻辑），包成一个
# nn.Module 塞进 validator。预处理/NMS/mAP 全走 ultralytics。
#
# ORT 优先用 CUDAExecutionProvider；不可用时自动 fallback 到 CPUExecutionProvider
# （此时用普通 sess.run + numpy，不能用 io_binding 绑 cuda 指针）。
#
# 动态 batch onnx；validator 固定 batch=1 / imgsz=640 / rect=False。
import os

_corex = "/usr/local/corex/lib64"
if os.path.isdir(_corex):
    os.environ["LD_LIBRARY_PATH"] = _corex + os.pathsep + os.environ.get("LD_LIBRARY_PATH", "")

import numpy as np
import torch
import onnxruntime as ort
from ultralytics.cfg import get_cfg
from ultralytics.utils import DEFAULT_CFG
from ultralytics.models.yolo.detect import DetectionValidator
from ultralytics.nn.modules.block import DFL
from ultralytics.utils.tal import dist2bbox, make_anchors

REG_MAX, NC = 16, 80
HEAD = [
    "/model.22/cv2.0/cv2.0.2/Conv_output_0", "/model.22/cv3.0/cv3.0.2/Conv_output_0",
    "/model.22/cv2.1/cv2.1.2/Conv_output_0", "/model.22/cv3.1/cv3.1.2/Conv_output_0",
    "/model.22/cv2.2/cv2.2.2/Conv_output_0", "/model.22/cv3.2/cv3.2.2/Conv_output_0",
]
DATA = os.environ.get("COCO_YAML", "/mnt/share/2/dataset/coco_val/coco_val.yaml")

# 注意：ort.get_available_providers() 会把 CUDA 列为 available，但其 .so 可能加载失败，
# 真正可信的信号是 session 建好后的 sess.get_providers()。所以 io_binding 与否按每个
# session 实际拿到的 provider 决定（见 NodecodeModel）。torch decode/baseline 用 cuda（torch GPU 正常）。
ORT_PROVIDERS = ["CUDAExecutionProvider", "CPUExecutionProvider"]
DEVICE = os.environ.get("DEVICE", "cuda" if torch.cuda.is_available() else "cpu")
NT = int(os.environ.get("NUM_THREADS", "16"))
torch.set_num_threads(NT)


class NodecodeModel(torch.nn.Module):
    """ORT 跑 6 个裸头张量 -> 补 decode -> 返回 [b,84,anchors]。CUDA EP 走 io_binding，否则普通 run。"""

    def __init__(self, onnx_path, names):
        super().__init__()
        so = ort.SessionOptions()
        so.intra_op_num_threads = NT
        so.inter_op_num_threads = 1
        self.sess = ort.InferenceSession(onnx_path, sess_options=so, providers=ORT_PROVIDERS)
        # CUDA EP 可能列为 available 却加载失败而静默退回 CPU；按实际 provider 决定是否 io_binding
        self.use_iobinding = ("CUDAExecutionProvider" in self.sess.get_providers()) and DEVICE == "cuda"
        if not self.use_iobinding:
            print(f"[warn] {os.path.basename(onnx_path)}: ORT 实际 providers={self.sess.get_providers()}，"
                  f"走 CPU sess.run（非 io_binding）", flush=True)
        self.device = torch.device(DEVICE)
        self.dfl = DFL(REG_MAX).eval().to(self.device)
        self.stride = torch.tensor([8.0, 16.0, 32.0], device=self.device)
        self._out_bufs = {}
        self._buf_key = None
        self.names = names
        self.pt = True
        self.fp16 = False

    def fuse(self, *a, **k):
        return self

    def _head_shapes(self, b, h, w):
        return {
            HEAD[0]: (b, 64, h // 8, w // 8),
            HEAD[1]: (b, 80, h // 8, w // 8),
            HEAD[2]: (b, 64, h // 16, w // 16),
            HEAD[3]: (b, 80, h // 16, w // 16),
            HEAD[4]: (b, 64, h // 32, w // 32),
            HEAD[5]: (b, 80, h // 32, w // 32),
        }

    def _ensure_out_bufs(self, im):
        b, _, h, w = im.shape
        key = (b, h, w)
        if self._buf_key == key:
            return
        shapes = self._head_shapes(b, h, w)
        self._out_bufs = {
            name: torch.empty(sh, dtype=torch.float32, device=self.device)
            for name, sh in shapes.items()
        }
        self._buf_key = key

    def _run_ort(self, im):
        if self.use_iobinding:
            self._ensure_out_bufs(im)
            io = self.sess.io_binding()
            io.bind_input("images", "cuda", im.device.index or 0, np.float32, tuple(im.shape), im.data_ptr())
            for name in HEAD:
                t = self._out_bufs[name]
                io.bind_output(name, "cuda", im.device.index or 0, np.float32, list(t.shape), t.data_ptr())
            self.sess.run_with_iobinding(io)
            return [self._out_bufs[n] for n in HEAD]
        # CPU EP：普通 run + numpy，再搬到 torch decode 设备
        inp = im.detach().contiguous().cpu().numpy().astype(np.float32)
        outs = self.sess.run(HEAD, {"images": inp})
        return [torch.from_numpy(o).to(self.device) for o in outs]

    def forward(self, im, *a, **k):
        if im.dim() == 3:
            im = im.unsqueeze(0)
        im = im.contiguous().float().to(self.device)
        outs = self._run_ort(im)
        no = 4 * REG_MAX + NC
        feats = [torch.cat([outs[2 * s], outs[2 * s + 1]], 1) for s in range(3)]
        anchors, strides = (t.transpose(0, 1) for t in make_anchors(feats, self.stride, 0.5))
        x_cat = torch.cat([f.view(im.shape[0], no, -1) for f in feats], 2)
        box, cls = x_cat.split((4 * REG_MAX, NC), 1)
        dbox = dist2bbox(self.dfl(box), anchors.unsqueeze(0), xywh=True, dim=1) * strides
        return torch.cat((dbox, cls.sigmoid()), 1)


def run(onnx_path, names):
    args = get_cfg(DEFAULT_CFG)
    args.data = DATA
    args.imgsz = 640
    args.batch = 1
    args.rect = False
    args.device = DEVICE
    args.conf = 0.001
    args.iou = 0.7
    args.split = "val"
    args.workers = 0
    args.verbose = False
    v = DetectionValidator(args=args)
    v(model=NodecodeModel(onnx_path, names))
    return float(v.metrics.box.map), float(v.metrics.box.map50)


def main():
    from ultralytics import YOLO
    b = YOLO("yolov8n.pt").val(
        data=DATA, imgsz=640, batch=1, rect=False, conf=0.001, iou=0.7,
        device=DEVICE, workers=0, verbose=False,
    )
    print(f"[baseline .pt {DEVICE}] mAP50-95={b.box.map:.4f}  mAP50={b.box.map50:.4f}", flush=True)
    names = YOLO("yolov8n.pt").model.names
    for path, tag in [("yolov8n_nodecode_dyn.onnx", "fp32 nodecode"),
                      ("yolov8n_nodecode_qdq_dyn.onnx", "int8-QDQ(ORT) nodecode"),
                      ("yolov8n_nodecode_qdq_modelopt_dyn.onnx", "int8-QDQ(modelopt) nodecode")]:
        m, m50 = run(path, names)
        print(f"[{tag}] mAP50-95={m:.4f}  mAP50={m50:.4f}", flush=True)


if __name__ == "__main__":
    main()
