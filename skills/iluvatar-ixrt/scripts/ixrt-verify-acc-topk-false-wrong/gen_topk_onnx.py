#!/usr/bin/env python3
# 生成最小 TopK onnx（scores[1,8400] -> TopK k=300 -> values[1,300] + indices[1,300]），
# 供 `ixrtexec --precision fp16 --verify_acc --ort_cpu` 复现「假报 WRONG」的 CLI 症状。
import onnx
from onnx import helper, TensorProto as T

N, K = 8400, 300
k_init = helper.make_tensor("k", T.INT64, [1], [K])
g = helper.make_graph(
    [helper.make_node("TopK", ["scores", "k"], ["values", "indices"],
                      axis=1, largest=1, sorted=1)],
    "topk",
    [helper.make_tensor_value_info("scores", T.FLOAT, [1, N])],
    [helper.make_tensor_value_info("values", T.FLOAT, [1, K]),
     helper.make_tensor_value_info("indices", T.INT64, [1, K])],
    [k_init],
)
m = helper.make_model(g, opset_imports=[helper.make_opsetid("", 13)])
m.ir_version = 8
onnx.save(m, "topk.onnx")
print("wrote topk.onnx")
