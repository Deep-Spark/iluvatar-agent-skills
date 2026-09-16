# ixrt-hook-dump-tensors — IxRT 调试 Hook 抓中间张量

## 目的

演示 IxRT 特有的**执行期 Hook** —— `IExecutionContext.register_hook`，在不改网络的前提下于每个算子执行前/后回调，dump 中间张量做精度定位。**NV TensorRT 公有 API 没有这个能力**（NV 侧只能靠 polygraphy 逐输出比，拿不到算子内部中间值）。

配套文档：[`../../references/hook.md`](../../references/hook.md)。

## 做了什么

`hook_dump.py` 用 ixrt Python API 手搭一个三算子网络：

```
x ──(mul2: ×2)──▶ ──(relu)──▶ ──(addbias: +arange(8))──▶ y
```

注册一个 `POSTRUN` hook，每个算子执行后用 `copy_ixrt_io_tensors_as_np(info, ort_style=True)` 把输出拷成 numpy，并与 numpy 参考实现逐元素 `allclose` 校验；再演示内置 `create_hook("print_info")`。

## 结果（实跑通过）

```
[POSTRUN] op_name='mul2' layer_type=<LayerType.???: -1> nin=2 nout=1
          out mul2_output    shape=(1, 8) dtype=float32 head=[-6. -4. -2.  0.  2.  4.]
[POSTRUN] op_name='relu' layer_type=<LayerType.???: -1> nin=1 nout=1
          out relu_output    shape=(1, 8) dtype=float32 head=[0. 0. 0. 0. 2. 4.]
[POSTRUN] op_name='addbias' layer_type=<LayerType.???: -1> nin=2 nout=1
          out y              shape=(1, 8) dtype=float32 head=[0. 1. 2. 3. 6. 9.]

input : [-3. -2. -1.  0.  1.  2.  3.  4.]
output: [ 0.  1.  2.  3.  6.  9. 12. 15.]
expect: [ 0.  1.  2.  3.  6.  9. 12. 15.]
OK: hook fired 3 ops, all intermediate tensors match numpy reference
```

内置 `print_info` 再为每个算子打印一张 `name/shape/paddings/dtype/format` 的 I/O 表。

## 关键发现（实测 API，对照官方文档）

- 注册：`ctx.register_hook(name, callback, trt.ExecutionHookFlag.POSTRUN)`；注销 `ctx.deregister_hook(name)`。时机 `PRERUN`/`POSTRUN`。
- 回调入参 `info` 字段是 **snake_case**（不是文档示意的 camelCase）：`op_name` / `op_type` / `layer_type` / `nb_inputs` / `nb_outputs` / `input_names` / `output_names` / `input_tensors` / `output_tensors`。
- **`op_name` 是可靠的算子标识**；本例里 `op_type` 为空串、`layer_type` 显示 `<LayerType.???: -1>`（elementwise/融合算子拿不到具名类型），定位时认 `op_name`。
- `copy_ixrt_io_tensors_as_np(info, ort_style=True)` 返回 **`{"input": [(name, np), ...], "output": [(name, np), ...]}`**（不是按位置索引的列表，也不是 dict-of-array）。
- `ort_style=True` 把 IxRT 内部布局转成 **LINEAR/FP32** 便于和 onnxruntime 逐元素对比；`False` 保留内部原始布局（含 padding/低精度）。
- 内置 hook 经 `from tensorrt.hook import create_hook` 取用（本例用的是 `print_info`）；同模块另有 `available_hooks()` 可列出内置项（如 `print_info` / `inject_external_input`），本例未调用。

## 运行

```bash
bash run.sh        # 依赖 ixrt Python binding（import tensorrt）和 cuda-python
```

驱动 ixrt python runtime，非编译 case，**不计入 build.sh 测试集**。
