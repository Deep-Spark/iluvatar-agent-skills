#!/usr/bin/env python3
# 构建并序列化 YoloDecodeNMS engine（fp16 / int8），供 trtexec/ixrtexec --loadEngine 基准测试。
import ctypes, os, sys
import numpy as np
import tensorrt as trt

_HERE = os.path.dirname(os.path.abspath(__file__))
PLUGIN_SO = os.environ.get("PLUGIN_SO", os.path.join(_HERE, "libyolo_decode_nms.so"))
ONNX_DIR  = os.environ.get("ONNX_DIR",  os.path.join(_HERE, "..", "onnx-qdq-export"))
NETSZ, MAXDET, CONF, IOU = 640, 300, 0.001, 0.7
BMIN, BOPT, BMAX = 1, 16, 32
HEAD = ["/model.22/cv2.0/cv2.0.2/Conv_output_0", "/model.22/cv3.0/cv3.0.2/Conv_output_0",
        "/model.22/cv2.1/cv2.1.2/Conv_output_0", "/model.22/cv3.1/cv3.1.2/Conv_output_0",
        "/model.22/cv2.2/cv2.2.2/Conv_output_0", "/model.22/cv3.2/cv3.2.2/Conv_output_0"]
OUT = ["num_dets", "det_boxes", "det_scores", "det_classes"]
LOG = trt.Logger(trt.Logger.ERROR)
ctypes.CDLL(PLUGIN_SO)
trt.init_libnvinfer_plugins(LOG, "")

ONNX = {"fp16": "yolov8n_nodecode_dyn.onnx", "int8": "yolov8n_nodecode_qdq_dyn.onnx"}

def build(mode):
    b = trt.Builder(LOG); net = b.create_network(0); p = trt.OnnxParser(net, LOG)
    onnx_path = os.path.join(ONNX_DIR, ONNX[mode])
    assert p.parse(open(onnx_path, "rb").read()), f"parse failed: {onnx_path}"
    obn = {net.get_output(i).name: net.get_output(i) for i in range(net.num_outputs)}
    ins = [obn[n] for n in HEAD]
    for t in ins: net.unmark_output(t)
    cr = trt.get_plugin_registry().get_plugin_creator("YoloDecodeNMS", "1", "")
    fc = trt.PluginFieldCollection([
        trt.PluginField("score_threshold", np.array([CONF], np.float32), trt.PluginFieldType.FLOAT32),
        trt.PluginField("iou_threshold",   np.array([IOU],  np.float32), trt.PluginFieldType.FLOAT32),
        trt.PluginField("max_det",  np.array([MAXDET], np.int32), trt.PluginFieldType.INT32),
        trt.PluginField("net_size", np.array([NETSZ],  np.int32), trt.PluginFieldType.INT32)])
    layer = net.add_plugin_v2(ins, cr.create_plugin("y", fc))
    for j, nm in enumerate(OUT): layer.get_output(j).name = nm; net.mark_output(layer.get_output(j))
    cfg = b.create_builder_config()
    if mode == "fp16": cfg.set_flag(trt.BuilderFlag.FP16)
    if mode == "int8": cfg.set_flag(trt.BuilderFlag.INT8)
    prof = b.create_optimization_profile()
    prof.set_shape("images", (BMIN,3,NETSZ,NETSZ), (BOPT,3,NETSZ,NETSZ), (BMAX,3,NETSZ,NETSZ))
    cfg.add_optimization_profile(prof)
    engine_bytes = b.build_serialized_network(net, cfg)
    out = os.path.join(_HERE, f"yolov8n_{mode}.engine")
    open(out, "wb").write(engine_bytes)
    print(f"saved: {out} ({os.path.getsize(out)//1024} KB)")

modes = sys.argv[1:] if sys.argv[1:] else ["fp16", "int8"]
for m in modes:
    print(f"building {m}..."); build(m)
