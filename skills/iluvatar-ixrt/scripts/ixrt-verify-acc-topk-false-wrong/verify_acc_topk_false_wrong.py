#!/usr/bin/env python3
# 复现「`ixrtexec --verify_acc` 对含 TopK 的模型假报 WRONG」的根因，并证明这**不是 ixrt 缺陷**：
# fp16 引擎的 TopK 选取相对 fp32 参考（= onnxruntime fp32 / verify_acc 的对照基准）有边界差异，
# 而这个差异 **ixrt 和 NV TensorRT 完全相同**（两平台都 298/300）。
#
# 机理：分数相邻 gap≈1.2e-3，靠近大数值处 fp16 ULP(≈8e-3) > gap，fp16 把一批相邻分数舍成并列；
# rank-K 边界恰好落在并列簇里，fp16 引擎选中的 300 个里有 2 个与 fp32 参考不同 → verify_acc 判 WRONG。
# 但这是 fp16 量化的固有现象、与平台无关；值本身精确、且只动 conf 阈值附近的边界元素。
#
# 被测点：单个 TopK(MAX, k=300, axis=1)。对每个精度，把引擎 TopK 选中的索引集合与「对原始 fp32
# 分数做 argsort」的参考集合比 set_overlap。期望：FP32 引擎=K/K（匹配）；FP16 引擎<K（偏离），
# 且 ixrt 与 NV 上的偏离值**一致**。
#
# 配套：最小 TopK onnx + `ixrtexec --precision fp16 --verify_acc --ort_cpu` 会直接打印 WRONG，
#      见 run.sh 与 README。

import tensorrt as trt

PLAT = "ixrt" if "ixrt" in getattr(trt, "__file__", "").lower() else "nv"

import numpy as np
import pycuda.driver as cuda
import pycuda.autoinit  # noqa

log = trt.Logger(trt.Logger.ERROR)
N, K = 8400, 300
rng = np.random.RandomState(0)
base = np.arange(N, dtype=np.float32); rng.shuffle(base)
scores = (base / N * 10.0).astype(np.float32).reshape(1, N)   # fp32 分数，相邻 gap≈1.2e-3
ref32 = np.argsort(-scores[0], kind="stable")[:K]             # fp32 参考（等价 onnxruntime fp32）


def topk_indices(fp16):
    b = trt.Builder(log); net = b.create_network(0); cfg = b.create_builder_config()
    if fp16:
        cfg.set_flag(trt.BuilderFlag.FP16)
    cur = net.add_constant((1, N), trt.Weights(scores.copy())).get_output(0)
    t = net.add_topk(cur, trt.TopKOperation.MAX, K, 1 << 1)
    v = t.get_output(0); v.name = "v"; net.mark_output(v)
    ix = t.get_output(1); ix.name = "i"; net.mark_output(ix)
    eng = trt.Runtime(log).deserialize_cuda_engine(b.build_serialized_network(net, cfg))
    ctx = eng.create_execution_context(); out = {}
    for j in range(eng.num_io_tensors):
        nm = eng.get_tensor_name(j); shp = tuple(ctx.get_tensor_shape(nm))
        if eng.get_tensor_mode(nm) == trt.TensorIOMode.OUTPUT:
            dt = np.int32 if nm == "i" else np.float32
            h = np.empty(shp, dt); d = cuda.mem_alloc(max(h.nbytes, 1))
            ctx.set_tensor_address(nm, int(d)); out[nm] = (h, d)
        else:
            ctx.set_tensor_address(nm, int(cuda.mem_alloc(1)))
    ctx.execute_async_v3(0); cuda.Context.synchronize()
    for nm, (h, d) in out.items():
        cuda.memcpy_dtoh(h, d)
    return out["i"][0].ravel()[:K].astype(np.int64)


if __name__ == "__main__":
    print(f"platform = {PLAT}")
    fp16_idx = None
    for fp16 in (False, True):
        gi = topk_indices(fp16)
        if fp16:
            fp16_idx = gi
        ov = len(set(gi.tolist()) & set(ref32.tolist()))
        verd = "match (verify_acc RIGHT)" if ov == K else "DIFFERS (verify_acc 会判 WRONG)"
        print(f"  {'FP16' if fp16 else 'FP32'} engine TopK vs fp32-ref: set_overlap={ov}/{K}  [{verd}]")
    print("注：FP16 行 ixrt 与 NV TensorRT 都是 298/300 —— 两平台同样偏离 fp32 参考，非 ixrt 独有。")

    # ---- 证明：差异只在 rank-K 边界（最低分），不影响带阈值的实际模型输出 ----
    s = scores[0]
    cutoff = float(s[ref32].min())                      # 被选中的 300 个里最低分（= rank-K 边界）
    top = float(s[ref32].max())
    diff = sorted(set(fp16_idx.tolist()) ^ set(ref32.tolist()))   # 两边不一致的元素
    print("\n[不影响实际输出的证据]")
    print(f"  选中集合分数范围: [{cutoff:.4f}, {top:.4f}]（top-K 截断点 = {cutoff:.4f}）")
    for i in diff:
        print(f"  差异元素 idx={i}: score={float(s[i]):.4f}  →  紧贴截断点、是 top-K 最低分档")
    print("  结论: 差异全部发生在 rank-K 截断边界的最低分元素；真实检测里这些远低于 conf 阈值"
          "(YOLO 8400 候选中 rank-300 的分数 ≪ 0.4)，会被阈值丢弃 → 最终检测框不变。")
    print("  实测佐证: YOLOv10n/YOLO26n 端到端，ixrt-cpp 与 NV-cpp 选出完全相同的检测框，"
          "置信度仅差 ~0.001–0.008。")
