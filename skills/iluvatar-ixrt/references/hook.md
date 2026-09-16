# IxRT 调试 Hook 接口：运行时抓中间张量

NV 的 TensorRT 公有 API **没有**执行期算子回调；IxRT 扩展了一个 **Hook 接口**，在不改网络的前提下，于每个算子执行前后回调，dump 中间张量做精度比对/定位。这是 IxRT 特有能力，无 NV 对应物。

时机两种：`kPRERUN`（算子执行前）/ `kPOSTRUN`（算子执行后）。

> 可跑用例：[`scripts/ixrt-hook-dump-tensors/`](../scripts/ixrt-hook-dump-tensors/) —— 手搭三算子网络、注册 POSTRUN hook 抓每个算子中间张量、与 numpy 参考逐元素校验，并演示内置 `print_info`。下面 Python 小节的字段名/返回结构均以该用例实测为准。

## C++

```cpp
void MyHook(nvinfer1::ExecutionContextInfo const* info) {
    cudaDeviceSynchronize();                 // ⚠️ 首行必须先同步，否则读到的是未完成的显存
    printf("%s (%s) in=%d out=%d\n",
           info->opName, info->op_type, info->nbInputs, info->nbOutputs);
    // info->inputNames / outputNames
    // info->inputTensors / outputTensors : ExecutionContextTensorDesc
    //   { void* data(device 指针); paddings; dims; type; format; float scale;
    //     bool is_initializer; bool is_shape_tensor; }
}
context->registerHook("my_hook", MyHook, nvinfer1::ExecutionHookFlag::kPOSTRUN);
```

- 想**改写**某个算子的输入显存只允许在 `kPRERUN`，且**仅限调试**，绝不可上生产。
- `ExecutionContextTensorDesc` 给的是 IxRT 内部布局（可能带 padding、非 LINEAR），直接 memcpy 出来要按 `format`/`paddings` 解释。

## Python

Python 侧统一写 `import tensorrt as trt`，当前 IxRT 包会加载同一套 binding。`tensorrt.hook` 是 IxRT 扩展接口。以下字段名/返回结构以同仓用例 [`scripts/ixrt-hook-dump-tensors/`](../scripts/ixrt-hook-dump-tensors/) 实测为准：

```python
import tensorrt
from tensorrt.hook.utils import copy_ixrt_io_tensors_as_np   # 也有 copy_ixrt_tensor_as_np(单张量)
from tensorrt.hook import create_hook                        # 内置 hook 在 tensorrt.hook，不在 .utils

def cb(info):
    # info 字段是 snake_case：op_name / op_type / layer_type /
    #   nb_inputs / nb_outputs / input_names / output_names / input_tensors / output_tensors
    io = copy_ixrt_io_tensors_as_np(info, ort_style=True)    # -> {"input":[(name,np)...], "output":[(name,np)...]}
    for name, arr in io["output"]:
        print(info.op_name, name, arr.shape, arr.ravel()[:6])

context.register_hook("cb", cb, tensorrt.ExecutionHookFlag.POSTRUN)
# context.deregister_hook("cb")
```

- **`op_name` 是可靠的算子标识**；`op_type` 常为空串、`layer_type` 对 elementwise/融合算子会显示 `<LayerType.???: -1>`，别依赖后两者。
- `copy_ixrt_io_tensors_as_np` 返回 **`{"input": [...], "output": [...]}`**（不是 `ins, outs` 两元组，更不是 dict-of-array）——按 `io["output"]` 取、元素是 `(name, ndarray)`。

| helper / 选项 | 作用 |
|---|---|
| `copy_ixrt_io_tensors_as_np(info, ort_style=)` | 把算子全部 I/O 拷成 numpy，返回 `{"input":[(name,np)], "output":[(name,np)]}` |
| `copy_ixrt_tensor_as_np(tensor, ort_style=)` | 拷单个张量（自动按 `is_shape_tensor` 走 shape/execution 转换） |
| `ort_style=True` | 转成 kLINEAR / FP32 标准布局，便于与 onnxruntime 逐元素对比 |
| `ort_style=False` | 保留 IxRT 内部原始布局（含 padding / 低精度） |
| `create_hook("print_info")` | 内置 hook：每个算子打印 name/shape/paddings/dtype/format I/O 表 |
| `create_hook("inject_external_input", external_input="x.npy")` | 内置 hook：把指定边输入替换成外部 npy（定位精度问题）。`available_hooks()` 列出全部内置 |

## 与 `--verify_acc` 的分工

- `ixrtexec --verify_acc`（见 [`ixrtexec.md`](./ixrtexec.md)）：一键逐层对 onnxruntime，给 RIGHT/WRONG 总判，**先用它定位哪层开始崩**。
- Hook：拿到具体某层的中间张量自己做细粒度比对 / 注入正确输入，**精确定位到算子内部**。`inject_external_input` 内置 hook 与 `--inject_tensors` 思路一致，但能在任意边上操作。
