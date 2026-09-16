# IxRT 运行时 API 与设施

本文收拢 Python runtime API、profiling、cuda-python、CUDA Graph 和多 optimization
profile。其余工具单独成文：命令行
`ixrtexec`（含 `--verify_acc`）见 [`ixrtexec.md`](./ixrtexec.md)，调试 Hook 见
[`hook.md`](./hook.md)。

## Profiling：kernel / 算子级耗时

两条路：

```bash
# A) NVTX + ixsys（天数对应 Nsight Systems 的工具），看 per-kernel 时间线
export IXRT_NVTX=ON
ixsys -o profiling.ptrace -t CUDA,NVTX bash inference.sh
#   产出 profiling.ptrace，导入 ixsys UI 框选区域看每个 kernel 耗时
```

```bash
# B) ixrtexec 内置逐层 profiler（详见 ixrtexec.md），给 Layer/Kernel/Time(ms)/Time(%) 表
ixrtexec --onnx model.onnx --precision fp16 --run_profiler --export_profiler result.csv
```

`IXRT_NVTX=ON` 让每个算子带 NVTX 标记，ixsys 才能把 GPU kernel 归到算子上。

## Python runtime API：legacy binding 与 name-based tensor

当前 IxRT 同时保留两套可用接口：

- legacy binding API：`num_bindings`、`get_binding_name`、`execute_async_v2`；
- name-based tensor API：`num_io_tensors`、`get_tensor_name`、
  `set_tensor_address`、`execute_async_v3`。

不要套用较新 NVIDIA TensorRT 的弃用口径，把 IxRT legacy binding API 描述成不可用。
跨平台共用代码可优先选择 name-based API 以减少接口分支；这只是跨平台一致性选择，
不代表 IxRT 的 legacy API 不受支持。

## cuda-python：配合 Python API 做显存搬运 / 同步

Corex CUDA driver+runtime 的 Cython 绑定（NV `cuda-python` 的移植），跟 IxRT Python API 搭配做 H2D/D2H、stream、event：

```python
import cuda.cudart as cudart      # runtime API
import cuda.cuda as cuda          # driver API
err, ptr = cudart.cudaMalloc(nbytes)          # 每个调用都返回 (err, ...) 元组
cudart.cudaMemcpy(ptr, host, nbytes, cudart.cudaMemcpyKind.cudaMemcpyHostToDevice)
```

**关键陷阱：只绑定下面白名单内的 API，列表外的不保证可用。**

| 类别 | 受支持的 runtime API（`cuda.cudart`） |
|---|---|
| 设备/内存 | `cudaSetDevice` `cudaMalloc` `cudaMallocHost` `cudaMallocAsync` `cudaMemset` `cudaMemcpy(+Async)` `cudaFree(+Host/+Async)` |
| stream | `cudaStreamCreateWithFlags` `cudaStreamSynchronize` `cudaStreamDestroy` `cudaStreamGetCaptureInfo` `cudaStreamBeginCapture` `cudaStreamEndCapture` |
| graph | `cudaGraphInstantiate` `cudaGraphExecDestroy` `cudaGraphDestroy` |
| event | `cudaEventCreateWithFlags` `cudaEventRecord` `cudaEventSynchronize` `cudaEventElapsedTime` `cudaEventDestroy` |
| 设备查询/P2P | `cudaGetDeviceCount` `cudaGetDeviceProperties` `cudaDeviceCanAccessPeer` `cudaDeviceEnablePeerAccess` `cudaDeviceReset` `cudaGetLastError` |
| IPC | `cudaIpcGetMemHandle` `cudaIpcOpenMemHandle` `cudaIpcCloseMemHandle` |

| 类别 | 受支持的 driver API（`cuda.cuda`） |
|---|---|
| 初始化/上下文 | `cuInit` `cuDeviceGet` `cuCtxCreate` `cuCtxDestroy` `cuDeviceGetAttribute` |
| 内存 | `cuMemAlloc` `cuMemcpyHtoD(+Async)` `cuMemcpyDtoH(+Async)` `cuMemFree` |
| stream/event | `cuStreamCreate` `cuStreamDestroy` `cuStreamSynchronize` `cuEventCreate` `cuEventRecord` `cuEventElapsedTime` |

覆盖能力：memcpy、stream（legacy/per-thread/non-blocking）、P2P peer access、CUDA graph 与 driver/runtime 互操作、stream capture、IPC mem handle、device attributes。配合 IxRT Python 推理（统一写 `import tensorrt as trt`）做 H2D/D2H/同步——推理流程本身与 TensorRT 一致。

## CUDA Graph：按模型验证

IxRT 内置 CUDA Graph 可通过环境变量开启：

```bash
export IXRT_USE_CUDA_GRAPH=1
python3 inference.py
```

该开关由 IxRT 读取，但“能开启”不等于“当前模型可正确使用”。必须对同一固定输入分别
关闭/开启 graph，比较输出 shape、finite 和数值，并确认没有 CPU fallback 或 capture
错误。动态 shape 频繁变化时收益小甚至无效。

## MultiOptimizationProfile：identity 双 profile 正样本

该 case 使用单文件脚本构建 identity engine：同一 engine 含 profile 0 / profile 1，
创建两个 execution context，分别激活一个 profile，并执行 `execute_async_v3` 验证输出
等于输入。脚本提供 `--network matmul-relu` 参数，可切换到 MatMul+ReLU 多 profile
模型做边界验证。

已验证 case：
[`scripts/ixrt-multi-optimization-profile/`](../scripts/ixrt-multi-optimization-profile/)。

```bash
bash skills/iluvatar-ixrt/scripts/ixrt-multi-optimization-profile/run.sh
```

结论：该 MultiOptimizationProfile identity runtime 用法在 IxRT 上可用。它证明非 0
profile 不是通用不可用；`--network matmul-relu` 在当前 IxRT 上仍会在 profile 1 激活
阶段触发 segfault，说明复杂模型、多 profile shape range、layer 组合仍需按模型单独验证。
