# IxRT MultiOptimizationProfile 正样本

## 目的

验证 IxRT 中同一 engine 的两个 optimization profile 可分别绑定到两个 execution
context，并完成 inference。脚本默认构建 identity network；也可通过 `--network
matmul-relu` 切换到 MatMul+ReLU 多 profile 模型，用于验证边界。

## 结果

PASS：当前 IxRT 可 build 含两个 optimization profile 的 identity engine；两个
execution context 分别激活 profile 0 / profile 1 后，均可设置动态输入 shape、执行
`execute_async_v3`，且输出与输入一致。

NVIDIA TensorRT 对照：同一单文件脚本也通过，profile 0 / profile 1 均输出匹配。

边界探针：`--network matmul-relu` 在 NVIDIA TensorRT 上 profile 0 / profile 1 均可
执行；在当前 IxRT 上 profile 0 可执行，切到 profile 1 时在 `BEFORE_SET_PROFILE:1`
之后触发 `Segmentation fault`，退出码为 `139`。

## 编译运行

```bash
bash run.sh
```

切换到 MatMul+ReLU 边界探针：

```bash
bash run.sh --network matmul-relu
```

## 关键输出

```text
BUILD_ENGINE_PASSED
AFTER_CREATE_CONTEXTS
Use optimization-profile 0
AFTER_SET_PROFILE:0
AFTER_SET_INPUT_SHAPE:0
AFTER_EXECUTE:0
OUTPUT_MATCH:0
Use optimization-profile 1
AFTER_SET_PROFILE:1
AFTER_SET_INPUT_SHAPE:1
AFTER_EXECUTE:1
OUTPUT_MATCH:1
PASS: MultiOptimizationProfile inference works
```

MatMul+ReLU 边界探针在当前 IxRT 上的关键输出：

```text
NETWORK:matmul-relu
BUILD_ENGINE_PASSED
AFTER_CREATE_CONTEXTS
Use optimization-profile 0
AFTER_SET_PROFILE:0
AFTER_EXECUTE:0
OUTPUT_UNCHECKED:0
Use optimization-profile 1
BEFORE_SET_PROFILE:1
Segmentation fault (core dumped)
```

## 证据边界

这个 case 能证明：identity network 的 MultiOptimizationProfile runtime 用法在 IxRT
上可用；同一脚本切换到 MatMul+ReLU 后可复现 profile 1 激活失败边界。

这个 case 不能证明：所有多 profile 模型都可用。MatMul+ReLU 边界探针已经说明复杂
layer 组合、不同 shape range 仍需按模型单独验证。
