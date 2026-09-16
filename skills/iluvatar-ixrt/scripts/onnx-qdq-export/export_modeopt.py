#!/usr/bin/env python3
# 与 export.py 同一条"decode + NMS 都不导出"路线，区别仅在第 3 步换量化后端：
#   1) yolov8n.pt --ultralytics(nms=False,dynamic=True)--> 临时 yolov8n.onnx（截断后删除）
#   2) onnx.utils.extract_model 把图截到检测头 6 个 leaf conv 输出（cv2.N.2 / cv3.N.2），
#      DFL 解码 + box/score Concat + NMS 全部不导出
#   3) 用 modelopt.onnx.quantization.quantize 做 INT8 QDQ
# decode + NMS 留给 IxRT 端自写插件（消费这 6 个裸头张量）。
#

import glob
import os

import cv2
import numpy as np
import onnx
from onnxruntime.quantization import CalibrationDataReader  # modelopt 复用 ORT reader，并额外调用 get_first()

from modelopt.onnx.quantization import quantize
from ultralytics import YOLO

ULTRALYTICS_RAW = "yolov8n.onnx"  # ultralytics 固定写此文件名，截断后删除
CUT = "yolov8n_nodecode_dyn.onnx"
QDQ = "yolov8n_nodecode_qdq_modelopt_dyn.onnx"  # 与 export.py 的 QDQ 区分，便于两后端对比
CALIB = "/mnt/share/2/dataset/int8calib-data/coco_calib/*.jpg"  # 1000 张 COCO 校准图

# ModelOpt 标定时跑 ONNX 推理用的后端优先级；"trt" 不是 IxRT 后端，Corex 侧用 cuda/cpu。
CALIB_EPS = os.environ.get("CALIB_EPS", "cuda:0,cpu").split(",")

# 检测头 6 个 leaf conv 输出（box 的 64 个 DFL bins + 80 类，3 个尺度），decode 之前
HEAD_OUTPUTS = [
    "/model.22/cv2.0/cv2.0.2/Conv_output_0", "/model.22/cv3.0/cv3.0.2/Conv_output_0",
    "/model.22/cv2.1/cv2.1.2/Conv_output_0", "/model.22/cv3.1/cv3.1.2/Conv_output_0",
    "/model.22/cv2.2/cv2.2.2/Conv_output_0", "/model.22/cv3.2/cv3.2.2/Conv_output_0",
]

from ultralytics.data.augment import LetterBox

# 抄 ultralytics/utils/export/onnx.py 的 _transform_fn：用 ultralytics LetterBox（scaleup=False、
# center=True、auto=False，与 val 一致）做几何，再 BGR->RGB、CHW、/255。
_LB = LetterBox(new_shape=(640, 640), scaleup=False)


def prep(p):
    im = _LB(image=cv2.imread(p))                  # 保持比例、居中 pad、不放大
    im = im[..., ::-1].transpose(2, 0, 1)          # BGR->RGB, HWC->CHW（同 ultralytics format）
    return (np.ascontiguousarray(im)[None].astype(np.float32)) / 255.0  # /255，同 _transform_fn


# Entropy(Histogram)标定会在内存累积中间张量输出，校准图过多会 OOM。
# 从 1000 张里随机抽 100 张（固定种子可复现）做标定。
CALIB_N = int(os.environ.get("CALIB_N", "100"))


class Reader(CalibrationDataReader):
    def __init__(self):
        import random
        all_paths = sorted(glob.glob(CALIB))
        rng = random.Random(0)
        self.paths = rng.sample(all_paths, min(CALIB_N, len(all_paths)))  # 随机抽样
        assert self.paths, f"未找到校准图片: {CALIB}"
        self.index = 0

    def get_next(self):
        if self.index >= len(self.paths):
            return None
        p = self.paths[self.index]
        self.index += 1
        return {"images": prep(p)} if p is not None else None

    def get_first(self):
        return {"images": prep(self.paths[0])}

    def rewind(self):
        self.index = 0


def _shape_str(dim):
    if dim.dim_param:
        return dim.dim_param
    if dim.dim_value:
        return str(dim.dim_value)
    return "?"


def print_model_io(path, tag):
    m = onnx.load(path)
    print(f"{tag} inputs:")
    for inp in m.graph.input:
        dims = [_shape_str(d) for d in inp.type.tensor_type.shape.dim]
        print(f"  {inp.name} [{', '.join(dims)}]")
    print(f"{tag} outputs:")
    for out in m.graph.output:
        dims = [_shape_str(d) for d in out.type.tensor_type.shape.dim]
        print(f"  {out.name} [{', '.join(dims)}]")


def export_raw():
    """ultralytics 导出到 yolov8n.onnx（中间文件，截断后删除）。"""
    YOLO("yolov8n.pt").export(
        format="onnx", opset=13, simplify=True, nms=False, dynamic=True,
    )
    return ULTRALYTICS_RAW


def cut_nodecode(raw_path, cut_path):
    onnx.utils.extract_model(raw_path, cut_path, input_names=["images"], output_names=HEAD_OUTPUTS)
    # 截断后再清一道图：先 onnxslim（结构化精简/融合），后 onnxsim（常量折叠/冗余消除）。
    # 与 export.py 的 cut_nodecode 完全一致，保证两后端量化基于同一张简化图、只差量化器。
    import onnxslim
    onnxslim.slim(cut_path, cut_path)
    import onnxsim
    sim, ok = onnxsim.simplify(onnx.load(cut_path))
    assert ok, "onnxsim 简化校验失败"
    onnx.save(sim, cut_path)
    cut = onnx.load(cut_path)
    bad = [n.op_type for n in cut.graph.node if n.op_type in {"Softmax", "NonMaxSuppression"}]
    assert not bad, f"解码段没切干净: {bad}"
    print_model_io(cut_path, f"截断 {cut_path}")


def quantize_nodecode(cut_path, qdq_path):
    # Model Optimizer 等价于 README 第三步的 Python API（quantize_mode=int8、entropy 标定）。
    # high_precision_dtype="fp32"：保持非量化部分 fp32，与 export.py 的 fp32+INT8 QDQ 对齐
    #   （默认 "fp16" 会把全图非量化张量转 fp16，改变模型）。
    # 校准数据走 calibration_data_reader（复用上面的 Reader，预处理与 eval 完全一致），
    #   而非 README 里的 calib.npy。
    quantize(
        onnx_path=cut_path,
        output_path=qdq_path,
        quantize_mode="int8",
        calibration_method="entropy",
        calibration_data_reader=Reader(),
        calibration_eps=CALIB_EPS,
        op_types_to_quantize=["MatMul", "Conv", "Gemm"],
        high_precision_dtype="fp32",
    )
    from onnx import TensorProto
    m = onnx.load(qdq_path)
    ops = [n.op_type for n in m.graph.node]
    n_i8 = sum(i.data_type == TensorProto.INT8 for i in m.graph.initializer)
    print(
        f"QDQ {qdq_path}: QuantizeLinear={ops.count('QuantizeLinear')}  "
        f"DequantizeLinear={ops.count('DequantizeLinear')}  int8权重={n_i8}"
    )


def main():
    print("\n=== dynamic batch (Model Optimizer) ===")
    raw = export_raw()
    cut_nodecode(raw, CUT)
    os.remove(raw)
    quantize_nodecode(CUT, QDQ)
    print(f"PASS: {CUT} + {QDQ}")


if __name__ == "__main__":
    main()
