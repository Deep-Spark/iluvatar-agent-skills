#!/usr/bin/env python3
# IxRT 调试 Hook 用例：运行时抓每个算子的中间张量。
#
# NV TensorRT 公有 API 没有执行期算子回调；IxRT 扩展了 IExecutionContext.register_hook。
# 本例用 ixrt Python API 手搭一个三算子网络（mul2 -> relu -> addbias），注册一个
# POSTRUN hook，在每个算子执行后把它的输出张量拷成 numpy（ort_style=True，转成
# LINEAR/FP32 标准布局），并与 numpy 参考实现逐元素对比，证明 hook 抓到的中间值正确。
# 最后再演示内置 hook create_hook("print_info")。
#
# 关键实测 API（ixrt 当前版本）：
#   ctx.register_hook(name, callback, trt.ExecutionHookFlag.PRERUN|POSTRUN)
#   ctx.deregister_hook(name)
#   回调入参 info 字段（snake_case）：op_name / op_type / layer_type /
#       nb_inputs / nb_outputs / input_names / output_names / input_tensors / output_tensors
#   from tensorrt.hook.utils import copy_ixrt_io_tensors_as_np
#       -> 返回 {"input":[(name,np),...], "output":[(name,np),...]}
#   from tensorrt.hook import create_hook, available_hooks   # 内置 print_info / inject_external_input
import numpy as np
import tensorrt as trt                                   # IxRT 的 python 模块名就是 tensorrt
from tensorrt.hook.utils import copy_ixrt_io_tensors_as_np
from tensorrt.hook import create_hook
from cuda import cudart


def cu(ret):
    err, *rest = ret
    assert err == cudart.cudaError_t.cudaSuccess, err
    return rest[0] if len(rest) == 1 else rest


def build_engine():
    log = trt.Logger(trt.Logger.ERROR)
    b = trt.Builder(log)
    net = b.create_network(1 << int(trt.NetworkDefinitionCreationFlag.EXPLICIT_BATCH))
    x = net.add_input("x", trt.float32, (1, 8))
    # op1: y = x * 2
    w = net.add_constant((1, 8), trt.Weights(np.full((1, 8), 2.0, np.float32)))
    mul = net.add_elementwise(x, w.get_output(0), trt.ElementWiseOperation.PROD)
    mul.name = "mul2"
    # op2: relu
    act = net.add_activation(mul.get_output(0), trt.ActivationType.RELU)
    act.name = "relu"
    # op3: + arange(8)
    bias = net.add_constant((1, 8), trt.Weights(np.arange(8, dtype=np.float32).reshape(1, 8)))
    add = net.add_elementwise(act.get_output(0), bias.get_output(0), trt.ElementWiseOperation.SUM)
    add.name = "addbias"
    add.get_output(0).name = "y"
    net.mark_output(add.get_output(0))
    plan = b.build_serialized_network(net, b.create_builder_config())
    return trt.Runtime(log).deserialize_cuda_engine(plan)


def main():
    eng = build_engine()
    ctx = eng.create_execution_context()

    x_np = np.arange(-3, 5, dtype=np.float32).reshape(1, 8)        # [-3..4]
    ref_mul = 2.0 * x_np                                          # mul2 输出参考
    ref_relu = np.maximum(ref_mul, 0.0)                          # relu 输出参考
    ref_y = ref_relu + np.arange(8, dtype=np.float32)            # 最终输出参考
    ref_out = {"mul2_output": ref_mul, "relu_output": ref_relu, "y": ref_y}

    captured = {}

    def dump(info):
        # ⚠️ hook 里读显存前框架已同步；ort_style=True 把内部布局转成 LINEAR/FP32 便于对比
        io = copy_ixrt_io_tensors_as_np(info, ort_style=True)
        print(f"[POSTRUN] op_name={info.op_name!r} layer_type={info.layer_type!r} "
              f"nin={info.nb_inputs} nout={info.nb_outputs}")
        for name, arr in io["output"]:
            captured[name] = arr
            print(f"          out {name:<14} shape={arr.shape} dtype={arr.dtype} head={arr.ravel()[:6]}")

    ctx.register_hook("dump", dump, trt.ExecutionHookFlag.POSTRUN)

    # --- 显存搬运 + 推理（cuda-python） ---
    nb = x_np.nbytes
    d_in = cu(cudart.cudaMalloc(nb))
    d_out = cu(cudart.cudaMalloc(nb))
    cudart.cudaMemcpy(d_in, x_np.ctypes.data, nb, cudart.cudaMemcpyKind.cudaMemcpyHostToDevice)
    ctx.set_tensor_address("x", int(d_in))
    ctx.set_tensor_address("y", int(d_out))
    st = cu(cudart.cudaStreamCreate())
    assert ctx.execute_async_v3(st), "execute_async_v3 failed"
    cudart.cudaStreamSynchronize(st)
    y = np.empty_like(x_np)
    cudart.cudaMemcpy(y.ctypes.data, d_out, nb, cudart.cudaMemcpyKind.cudaMemcpyDeviceToHost)

    print("\ninput :", x_np.ravel())
    print("output:", y.ravel())
    print("expect:", ref_y.ravel())

    # --- 校验：最终输出 + 每个被 hook 抓到的中间张量都与 numpy 参考一致 ---
    assert np.allclose(y, ref_y, atol=1e-4), "final output mismatch"
    for name, ref in ref_out.items():
        assert name in captured, f"hook missed tensor {name}"
        assert np.allclose(captured[name], ref, atol=1e-4), f"intermediate {name} mismatch"
    print(f"\nOK: hook fired {len(captured)} ops, all intermediate tensors match numpy reference")

    # --- 内置 hook：print_info（打印每个算子 I/O 表）---
    print("\n=== built-in create_hook('print_info') ===")
    ctx.deregister_hook("dump")
    ctx.register_hook("pi", create_hook("print_info"), trt.ExecutionHookFlag.POSTRUN)
    ctx.execute_async_v3(st)
    cudart.cudaStreamSynchronize(st)


if __name__ == "__main__":
    main()
