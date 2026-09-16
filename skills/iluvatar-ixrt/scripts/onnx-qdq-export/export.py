#!/usr/bin/env python3
# "decode + NMS 都不导出"路线：
#   1) yolov8n.pt --ultralytics(nms=False,dynamic=True)--> 临时 yolov8n.onnx（截断后删除）
#   2) onnx.utils.extract_model 把图截到检测头 6 个 leaf conv 输出（cv2.N.2 / cv3.N.2），
#      DFL 解码 + box/score Concat + NMS 全部不导出
#   3) 整图量化（无需 nodes_to_exclude —— 没有解码头，box+score 共享 scale 的归零坑不存在）
# decode + NMS 留给 IxRT 端自写插件（消费这 6 个裸头张量）。
import glob
import os

import cv2
import numpy as np
import onnx
import onnxruntime as ort


def _parse_version(s):
    import re
    m = re.match(r"(\d+)\.(\d+)\.(\d+)", s.split("+")[0])
    if not m:
        raise SystemExit(f"无法解析版本号: {s}")
    return tuple(int(x) for x in m.groups())


def _check_quant_compat():
    ort_v = _parse_version(ort.__version__)
    onnx_v = _parse_version(onnx.__version__)
    ort_s, onnx_s = ort.__version__, onnx.__version__
    v118 = (1, 18, 0)
    if ort_v < v118:
        if onnx_v > v118:
            raise SystemExit(
                f"onnxruntime {ort_s} < 1.18.0 的量化器仍引用 onnx.mapping，"
                f"需 onnx ≤ 1.18，当前 onnx {onnx_s}。"
                "请安装: pip install 'onnx<=1.18'"
            )


_check_quant_compat()

from onnxruntime.quantization import (
    quantize_static, QuantFormat, QuantType, CalibrationMethod, CalibrationDataReader,
)
from onnxruntime.quantization.shape_inference import quant_pre_process
from ultralytics import YOLO

ULTRALYTICS_RAW = "yolov8n.onnx"  # ultralytics 固定写此文件名，截断后删除
CUT = "yolov8n_nodecode_dyn.onnx"
PRE = "yolov8n_nodecode_pre_dyn.onnx"
QDQ = "yolov8n_nodecode_qdq_dyn.onnx"
CALIB = "/mnt/share/2/dataset/int8calib-data/coco_calib/*.jpg"  # 1000 张 COCO 校准图

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
        chosen = rng.sample(all_paths, min(CALIB_N, len(all_paths)))  # 随机抽样
        self.paths = iter(chosen)  # 惰性逐张

    def get_next(self):
        p = next(self.paths, None)
        return {"images": prep(p)} if p is not None else None


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
    # 截断后再清一道图：先 onnxslim（结构化精简/融合），后 onnxsim（常量折叠/冗余消除），
    # 让量化基于更干净的图，QDQ 更易成对、TRT 融合更彻底。
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


def pre_process(cut_path, pre_path):
    # onnxruntime 上游函数 quant_pre_process：量化前对 float32 图做预处理，三步均可单独跳过——
    #   1) 符号形状推断（transformer 动态 shape 必需，skip_symbolic_shape=False）
    #   2) 图优化（常量折叠 / 算子融合；只在此做，量化阶段不再二次优化，便于逐层定位精度）
    #   3) ONNX 形状推断（补全剩余张量静态 shape）
    # 缺 shape 的节点会量化失败或精度异常，故 quantize_static 前先跑。
    # 等价 CLI: python -m onnxruntime.quantization.preprocess --input <cut> --output <pre>
    quant_pre_process(
        input_model_path=cut_path,
        output_model_path=pre_path,
        skip_symbolic_shape=False,   # CNN 可设 True；transformer 必须 False
        skip_optimization=False,
        skip_onnx_shape=False,
        auto_merge=True,             # 符号推断遇维度冲突时自动合并，避免直接报错
    )
    onnx.checker.check_model(pre_path)   # 产物必须是合法 ONNX，否则抛异常
    # 注：本例 CUT 已经 onnxslim+onnxsim 清过、形状基本齐全，预处理后 value_info 变化很小
    # （图优化可能再折叠个别节点）；预处理的价值主要体现在未清理过的原始导出图上。
    n_vi = len(onnx.load(pre_path).graph.value_info)
    print(f"预处理 {pre_path}: 合法 ONNX，value_info={n_vi}（quant_pre_process 形状推断+图优化）")


def quantize_nodecode(cut_path, qdq_path):
    quantize_static(
        cut_path, qdq_path, Reader(),
        quant_format=QuantFormat.QDQ, per_channel=True,
        activation_type=QuantType.QInt8, weight_type=QuantType.QInt8,
        calibrate_method=CalibrationMethod.Entropy,
        op_types_to_quantize=["MatMul", "Conv", "Gemm"],
        extra_options={
            # —— 以下 3 个是 TensorRT/IxRT 兼容必需 ——
            "ActivationSymmetric": True,  # TRT 只收对称量化，否则报 "Non-zero zero point"
            "WeightSymmetric": True,      # 权重对称
            "QuantizeBias": False,        # per_channel 会把 bias 量成 INT32+DQ，TRT 拒收 INT32 DQ
            # —— 以下为无效/no-op，仅按要求保留（onnxruntime 静默忽略未知 key）——
            "ZeroPoint": 0,               # 非合法 key；对称已强制 zp=0
            "EnableSubgraph": True,       # 仅控制流子图用，YOLO 无此结构
            "Axis": False,                # 非合法 key；per_channel 的 axis 自动确定，不会因此关闭
        },
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
    print("\n=== dynamic batch ===")
    raw = export_raw()
    cut_nodecode(raw, CUT)
    os.remove(raw)
    pre_process(CUT, PRE)
    quantize_nodecode(PRE, QDQ)
    print(f"PASS: {CUT} + {PRE} + {QDQ}")


if __name__ == "__main__":
    main()
