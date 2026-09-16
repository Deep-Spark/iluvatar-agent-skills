---
name: iluvatar-ixrt
description: >-
  Iluvatar Corex 的 **IxRT** 迁移与使用参考——**只讲与 NVIDIA TensorRT 的不同**，行为一致的 API 不收录，多数条目带自包含 repro。
  覆盖 TensorRT→IxRT 库/头映射、CMake 模板（一份 CMakeLists 两平台通吃）、`FindTensorRT.cmake`→`FindIxRT.cmake`
  迁移（继续输出 `TensorRT::TensorRT` 兼容 target）、内置 plugin creator registry、
  INT8 量化（隐式 dynamic-range / 显式 Q/DQ）、`ixrtexec`（NV `trtexec` 对应物）+ `--verify_acc` 及 TopK 假阳性陷阱、
  版本宏门控陷阱（`NV_TENSORRT_VERSION`）、不支持接口/枚举/算子总表、78 个内置 plugin creator、Hook / profiling 等工具、
  IxRT/推理部署参考代码仓库清单（deepsparkinference / iluvatar-corex-ixrt / triton / tensorrtx 等）。
  触发词：TensorRT 工程迁 corex、TensorRT 换 ixrt、FindTensorRT 改 FindIxRT、find_package(IxRT)、TensorRT::TensorRT 兼容 target、
  CMAKE_CUDA_COMPILER_ID 判断 ILUVATAR、IxRT plugin、IPluginV3、addPluginV3、ixrt 内置插件、ixrtexec / trtexec 对应、
  ixrt INT8 量化 / set_dynamic_range / Q/DQ、verify_acc 假报 WRONG、TopK 精度对不上、ixrt 不支持哪些接口/算子、
  ixrt hook 抓中间结果、ixrt/天数 推理参考代码 / 开源仓库 / 示例工程、deepsparkinference/tensorrtx/triton corex。
---

# Iluvatar IxRT：推理栈迁移与使用参考

Corex 自带一套推理栈 **IxRT**。上游用 `TensorRT` 写的工程多数可零源改迁移。

## 库 / 头映射

IxRT 把功能拆成多个轻量 `.so`（与 NV TensorRT 的拆分基本一一对应），按场景链最小集即可。Corex 侧头文件在 `$COREX/include`、库在 `$COREX/lib64`（`$COREX` = `/usr/local/corex`）：

| 角色 | NV 原生 | Corex (IxRT) |
|---|---|---|
| 头文件 | `NvInfer.h` | `NvInfer.h` |
| 推理核心 | `libnvinfer.so` | `libixrt.so` |
| 精简运行时 | `libnvinfer_lean.so` | `libixrt_lean.so` |
| 编译期资源（体积大，仅构建引擎时需要） | `libnvinfer_builder_resource_sm*.so`（NV 按 SM 架构再拆成多个） | `libixrt_builder_resource.so`（单个） |
| plugin 库 | `libnvinfer_plugin.so` | `libixrt_plugin.so` |
| onnx parser | `libnvonnxparser.so` | `libixrtonnxparser.so` |
| python 包 | `tensorrt` | `ixrt` |

**按需链接最小集**：纯推理 = `libixrt` + `libixrt_lean`；端上解析 ONNX → 再加 `libixrtonnxparser`；用到内置/自定义 plugin → 再加 `libixrt_plugin`。

> 较新的 NV TensorRT 另有 IxRT 无对应的 `libnvinfer_dispatch.so`（最小派发运行时）与
> `libnvinfer_vc_plugin.so`（版本兼容插件）；IxRT 未拆这两个。

`NvInferVersion.h` 报告的是 IxRT 自己的版本 `NV_TENSORRT_MAJOR.MINOR.PATCH.BUILD = 1.1.0.0`，不是 NV 语义版本。结构也有别：IxRT 保留老的 `NV_TENSORRT_SONAME_*` 宏、没有 NV 新增的 `NV_TENSORRT_LWS_*` / `NV_TENSORRT_RELEASE_TYPE`。详见下节「版本宏门控陷阱」。

## 版本宏门控陷阱

NV 头里没有 `#if NV_TENSORRT_VERSION >= …` 的条件编译，但**下游代码**（tensorrtx、用户工程、plugin 序列化）大量用它做能力门控。换算宏两平台一致：

```c
#define NV_TENSORRT_VERSION  (NV_TENSORRT_MAJOR*10000 + NV_TENSORRT_MINOR*100 + NV_TENSORRT_PATCH)
```

| | MAJOR.MINOR.PATCH | `NV_TENSORRT_VERSION` |
|---|---|---|
| IxRT | 1.1.0 | **10100** |
| NV stock | 10.16.1 | **101601** |

根源：IxRT 的 major=1，但 `1*10000+1*100=10100` **数值上大于真正的 TRT 8.x（8000–8699）、7.x（7000+），却小于 TRT 10.x（≥100000，六位数）**。于是版本门控把 IxRT 误读成「比 8.x 新、比 10 旧」——纯属编码巧合，不可依赖。下面是一个实测样本：

### 实测样本：`TRT_NOEXCEPT` 被门控误关 → plugin 全线编译错

部分上游 plugin 工程（典型如 **tensorrtx**，在各自的 `macros.h` 里）用一个版本门控宏 `TRT_NOEXCEPT` 给 override 方法加 `noexcept`，以兼容 TRT 7 以前（那时虚函数还不是 `noexcept`）。

```cpp
// tensorrtx 各工程 macros.h 里的典型写法
#if NV_TENSORRT_MAJOR >= 8
#define TRT_NOEXCEPT noexcept
#else
#define TRT_NOEXCEPT          // 空
#endif
```

IxRT 上 `NV_TENSORRT_MAJOR == 1` → 走 else 分支 → `TRT_NOEXCEPT` 展开为**空**。于是所有 override 方法丢掉 `noexcept`，而 IxRT 的 `NvInfer.h` 基类虚函数本身是带 `noexcept` 声明的，子类异常规格「比基类更宽松」，`clang++` 对每个 override 报错：

```
error: exception specification of overriding function is more lax than base version
```

**IxRT 其实支持 `noexcept`**（它的头就用 noexcept 声明），但版本号编码成 `1.x` 让 `>= 8` 门控把它当成「TRT 7 以前」，反而把本该启用的 `noexcept` 关掉了——这是版本门控的一种「反向」误判：不是误开不存在的 API，而是误关本可用的特性。

规避：别拿版本号判，直接无条件给 `noexcept`：

```cpp
#ifndef TRT_NOEXCEPT
#define TRT_NOEXCEPT noexcept     // IxRT / NV 10.x 的虚函数都是 noexcept，无条件即可
#endif
```

完整可跑示例见 [`scripts/cmake-corex-ixrt-template/nonZeroPlugin.h`](scripts/cmake-corex-ixrt-template/nonZeroPlugin.h)。

## CMakeLists 改法（TensorRT 工程迁到 IxRT）

迁移原则、CMake 与构建脚本的职责边界见
[`references/cmake-migration.md`](references/cmake-migration.md)；可运行模板见
[`scripts/cmake-corex-ixrt-template/`](scripts/cmake-corex-ixrt-template/) 和
[`scripts/cmake-findixrt-migration/`](scripts/cmake-findixrt-migration/)。

> 构建/推理 API、动态 shape（`OptimizationProfile`/`setInputShape`/`IOutputAllocator`/Shape Tensor）、强弱类型精度约束（`kPREFER_/kOBEY_PRECISION_CONSTRAINTS`）等**与 TensorRT 行为一致**，照 NV 写法即可，本 skill 不复述。少数 IxRT 差异已并入下面各节（如 Python 包名兼容、data-dependent 输出的非空-ptr 约束、If/Loop 控制流限制）。

## 已知不支持 / 缺实现

**迁移前先扫「不支持总表」**——IxRT 当前 public API 仍有一批 TensorRT 接口未提供，或虽有声明但最小 build/runtime 路径不可用。未提供 / 不支持接口、枚举和精度限制、算子与控制流限制、迁移语义变化、78 个内置 plugin creator、INT8 量化与「int8 不一定更快」等导航，统一见 [`references/unsupported-interfaces.md`](references/unsupported-interfaces.md)。

## 命令行工具（ixrtexec）

NV 的 `trtexec` 在 Corex 上对应 **`ixrtexec`**（构建引擎 / 跑基准 / 精度验证）。完整参数对照表
（`--save_engine`、`--precision`、动态 shape、INT8、`--verify_acc` 等）与典型用法单独成文：
见 [`references/ixrtexec.md`](references/ixrtexec.md)。

**精度验证**：`ixrtexec --verify_acc`（用 onnxruntime 当参考，逐层比余弦/最大差，量化「ixrt 掉多少精度」的首选工具）、含 TopK 模型的假阳性陷阱、以及实测样例，见 [`references/ixrtexec.md`](references/ixrtexec.md) 的「精度验证」节。

## 其他工具与运行时设施

- **调试 Hook** — `registerHook` 运行时抓中间张量（C++/Python，NV TensorRT 公有 API 没有），见 [`references/hook.md`](references/hook.md)。
- **运行时设施** — Python runtime API、profiling、cuda-python、CUDA Graph 与多 profile，见 [`references/runtime-tools.md`](references/runtime-tools.md)。

## 参考代码仓库

迁移 / 部署时可对照的外部开源仓库（IxRT 引擎源码、推理样例、Triton 服务、视频 pipeline、量化导出、网络手搭）：

- **官方源码 / 模型库** — [iluvatar-corex-ixrt](https://gitee.com/deep-spark/iluvatar-corex-ixrt)（IxRT 开源版引擎源码）、[deepsparkinference](https://gitee.com/deep-spark/deepsparkinference)（天数智芯 GPU 推理模型库与精度-性能基线）。
- **服务 / pipeline 部署** — [triton-inference-server-build](https://gitee.com/121786404/triton-inference-server-build)（Triton 在 Corex 上的构建/后端适配）、[iluvatarpipeline](https://gitee.com/121786404/iluvatarpipeline)、[trt_yolo_video_pipeline](https://github.com/121786404/trt_yolo_video_pipeline)（YOLO 视频流端到端 pipeline）。
- **量化 / 网络搭建** — [Model-Optimizer](https://gitee.com/121786404/Model-Optimizer)（ONNX QDQ/FP8/INT4 量化导出 fork）、[tensorrtx](https://gitee.com/121786404/tensorrtx)（TensorRT API 手搭网络，IxRT 插件与迁移参考）。

需要"参考代码 / 开源仓库 / 示例工程 / 有没有现成的 XX 项目"时查本节。

## 参考文档导航

| 文档 | 内容 |
|---|---|
| [`references/cmake-migration.md`](references/cmake-migration.md) | TensorRT→IxRT 的 CMake 迁移、构建脚本职责边界与禁止软链接伪装库名 |
| [`references/unsupported-interfaces.md`](references/unsupported-interfaces.md) | 未提供或探针不可用接口 / 不支持枚举·算子 / 算子能力限制 / 迁移语义变化总表 |
| [`references/builtin-plugins.md`](references/builtin-plugins.md) | 当前安装 IxRT 实测的 78 个内置 plugin creator 目录（注册名 / 版本 / 字段查询 / 静态上限字段） |
| [`references/ixrt-int8-quant.md`](references/ixrt-int8-quant.md) | INT8 对称量化：隐式 dynamic-range / 显式 Q/DQ / 无在线 calibrator |
| [`references/onnx-qdq-export.md`](references/onnx-qdq-export.md) · [`onnx-qdq-export-modeopt.md`](references/onnx-qdq-export-modeopt.md) | 离线导出 QDQ ONNX（ORT / Model Optimizer 两条路线） |
| [`references/ixrtexec.md`](references/ixrtexec.md) | `ixrtexec` ↔ `trtexec` 参数对照、builder optimization 验收策略、`--verify_acc` 与 polygraphy |
| [`references/hook.md`](references/hook.md) | 调试 Hook 接口：`registerHook` 运行时抓中间张量（IxRT 特有） |
| [`references/runtime-tools.md`](references/runtime-tools.md) | Python runtime API / profiling / cuda-python / CUDA Graph / 多 profile |

---

## 案例索引

| 案例 | 驱动 | 内容 |
|---|---|---|
| [`cmake-corex-ixrt-template/`](scripts/cmake-corex-ixrt-template/) | `build.sh` | TensorRT→IxRT 迁移的 CMake 模板：一份 CMakeLists 两平台通吃，含 nonZero plugin 可跑样例 |
| [`cmake-findixrt-migration/`](scripts/cmake-findixrt-migration/) | `FindIxRT.cmake` | 已有 `FindTensorRT.cmake` 的工程迁 Corex：新增 `FindIxRT.cmake`，只输出 TensorRT 兼容变量/targets，避免污染 NV 默认构建路径 |
| [`onnx-qdq-export/`](scripts/onnx-qdq-export/) | `export.py` | 导出"裸头"YOLOv8 QDQ ONNX（decode/NMS 不进图），ORT 与 Model Optimizer 两条 INT8 量化路线，附 mAP/eval |
| [`yolo-decode-nms-plugin/`](scripts/yolo-decode-nms-plugin/) | `build.sh` | YOLO decode + EfficientNMS 自写 IxRT 插件，消费裸头 6 输出 |
| [`ixrt-verify-acc-topk-false-wrong/`](scripts/ixrt-verify-acc-topk-false-wrong/) | `run.sh` | `--verify_acc` 对含 TopK 模型假报 WRONG 的复现与消除（per-output 比较） |
| [`ixrt-layer-profiling/`](scripts/ixrt-layer-profiling/) | `run.sh` | `ixrtexec --run_profiler` 逐层耗时 + ixsys kernel 名，定位「IxRT 上 int8 反而比 fp16 慢」的根因（int8 conv 要求 `DQ→Conv→Act→Q` 收口） |
| [`ixrt-hook-dump-tensors/`](scripts/ixrt-hook-dump-tensors/) | `run.sh` | IxRT 特有执行期 Hook（NV 无）：`register_hook` POSTRUN 抓每个算子中间张量、与 numpy 逐元素校验 + 内置 `print_info`（配 [`references/hook.md`](references/hook.md)） |
| [`ixrt-multi-optimization-profile/`](scripts/ixrt-multi-optimization-profile/) | `run.sh` | 双 profile identity 正样本与 MatMul+ReLU 边界 |
