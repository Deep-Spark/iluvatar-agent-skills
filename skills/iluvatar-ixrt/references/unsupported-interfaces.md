# IxRT 当前版本不支持 / 未提供的接口与能力

本页按当前安装的 IxRT 包、public C++ header 和 Python binding 修订。这里的“不支持”包含两种情况：

- 当前头文件 / Python binding 没有提供对应 TensorRT 接口或枚举。
- 头文件有声明，但最小 build / parser / runtime 探针确认不可用。

不要只看头文件声明下结论；最终以最小网络 build、反序列化或 parser 探针为准。

## 已从不支持表移除的旧结论

下面这些旧条目在当前版本已经可用，不能再写成不支持：

| 项 | 当前结论 |
|---|---|
| `IPluginV3` / `IPluginCreatorV3One` / `addPluginV3` | 支持用户自定义 V3 plugin 的 `registerCreator`、`addPluginV3`、`buildSerializedNetwork`、`deserializeCudaEngine` 最小路径；IxRT 自带内置插件当前仍注册为 `IPluginCreator` |
| `IEngineInspector` | C++ `engine->createEngineInspector()` 可返回非空，`getEngineInformation(kONELINE)` 可返回 engine 信息；Python binding 当前未暴露 `IEngineInspector` |
| ONNX parser `parse` / `parseFromFile` / `parseWithWeightDescriptors` / `supportsOperator` | 最小 ONNX Identity 均可解析；`supportsOperator("Identity") == true` |
| `IBuilderConfig::createTimingCache` / `setTimingCache` / `getTimingCache` | C++ 最小探针可创建、设置、读取、序列化 timing cache |
| `IBuilderConfig::setMemoryPoolLimit` / `getMemoryPoolLimit` | `WORKSPACE`、`TACTIC_DRAM`、`TACTIC_SHARED_MEMORY` 等可设置并读回 |
| `ICudaEngine::getProfilingVerbosity` | 可用 |
| `IExecutionContext::getTensorStrides` | 可用，最小静态 shape engine 返回有效 stride |
| C++ `IRuntime::setTemporaryDirectory` / `getTemporaryDirectory` | 可用；Python binding 以 `Runtime.temporary_directory` 属性形式暴露 |
| Conv padding mode / 3D deconv / resize cubic / pooling blend / unary NOT / Einsum INT8 | 最小 build 均可生成 serialized engine |

## 当前不支持 / 未提供的接口

| 归属 | 当前不可用项 | 说明 / 替代 |
|---|---|---|
| INT8 在线校准 | `IInt8Calibrator`、`IInt8EntropyCalibrator*`、`IInt8MinMaxCalibrator`、`IBuilderConfig::setInt8Calibrator/getInt8Calibrator` | 当前 public API 不提供在线 calibrator；INT8 走离线 scale / QDQ，见 [`ixrt-int8-quant.md`](./ixrt-int8-quant.md) |
| Refit | `BuilderFlag::kREFIT*`、公开 `createInferRefitter` / `ICudaEngine::createRefitter` 入口 | `IRefitter` / `IParserRefitter` 类有声明，`ICudaEngine::isRefittable()` 可调用；但当前 public wrapper 没有可用的 refit 构建与创建入口，普通 engine `isRefittable() == false` |
| 算法选择 / tactic source | `IAlgorithmSelector` / `IAlgorithm*`、`IBuilderConfig::setTacticSources/getTacticSources` | 当前 public API 不提供手动 tactic 选择 |
| DLA 执行路径 | `setDLACore/getDLACore`、`setDefaultDeviceType`、`setDeviceType`、`canRunOnDLA`、GPU fallback 相关 flag | 当前没有 DLA 执行选择入口；`DeviceType::kDLA` 和 DLA memory pool enum 不代表存在 DLA 执行能力 |
| Debug tensor / runtime debug | `INetworkDefinition::markDebug/unmarkDebug/isDebugTensor`、`ICudaEngine::isDebugTensor`、`IExecutionContext::setTensorDebugState` | 当前 public API 不提供 TensorRT debug tensor 路径 |
| 进度 / 检查器 | `IProgressMonitor`、`IConsistencyChecker`、`IPluginChecker`、builder config progress monitor 入口 | 当前 public API 不提供 |
| Version-compatible / lean runtime | `IRuntime::loadRuntime`、`setEngineHostCodeAllowed`、`setTempfileControlFlags/getTempfileControlFlags`、`IBuilderConfig::setPluginsToSerialize`、`ICudaEngine::serializeWithConfig` | 当前不提供 NV TensorRT version-compatible engine / lean runtime 组合能力 |
| Aux streams / profile stream | `IBuilderConfig::setMaxAuxStreams/getMaxAuxStreams`、`IBuilderConfig::setProfileStream/getProfileStream`、`ICudaEngine::getNbAuxStreams` | 当前 public API 不提供 |
| Safety scope | `EngineCapability`、`IOnnxConfig`、`getBuilderSafePluginRegistry` | 当前不提供 safety / safe runtime 构建路径 |
| `IStreamReader` | streaming deserialize reader | C++ 头文件声明了 `IStreamReader` 和 `IRuntime::deserializeCudaEngine(IStreamReader&)`，但当前实测返回空并报 `Do not support using IStreamReader to deserialize cuda engine`；Python binding 未暴露 `IStreamReader` |

## 当前不支持 / 不作为通用能力的枚举和精度

| 类别 | 当前结论 |
|---|---|
| `BuilderFlag` | 当前 C++ 头文件只提供 `kFP16`、`kINT8`、`kBF16`、`kDISABLE_TIMING_CACHE`、`kPREFER_PRECISION_CONSTRAINTS`、`kOBEY_PRECISION_CONSTRAINTS`、`kERROR_ON_TIMING_CACHE_MISS`。NV 的 `kDEBUG`、`kGPU_FALLBACK`、`kREFIT*`、`kTF32`、`kSPARSE_WEIGHTS`、`kFP8`、`kVERSION_COMPATIBLE`、`kWEIGHT_STREAMING`、`kDIRECT_IO`、`kSAFETY_SCOPE`、`kSTRIP_PLAN`、`kEXCLUDE_LEAN_RUNTIME`、`kREJECT_EMPTY_ALGORITHMS` 当前头文件不提供 |
| `DataType` / precision | `ixrtexec --precision` 只接受 `int8/fp16/bf16/fp32`。`DataType::kFP8`、`kINT4`、`kFP4` 虽在 C++ enum 中声明，但没有对应 builder flag / ixrtexec precision；`addQuantize(..., kFP8)` 最小 build 失败 |
| `TensorFormat` | C++ enum 声明了多种 format，但当前 `ITensor::setAllowedFormats/getAllowedFormats` 不在 public header；最小 engine I/O format 为 `kLINEAR` / `t_linear`。不要把 CHW/HWC/DHWC 等格式当成可由用户稳定选择的通用能力 |
| `SerializationFlag` / `TempfileControlFlag` | enum 有声明，但缺少 version-compatible / lean runtime 相关 public API 入口，不能按 NV TensorRT 版本兼容路径使用 |

## 当前不支持的算子 / 网络控制流

| 类别 | 当前结论 | 替代 |
|---|---|---|
| Loop 控制流 | `INetworkDefinition::addLoop()` 当前返回空并报 `The addLoop method is not implemented.`；`TRIP_LIMIT` / `RECURRENCE` / `ITERATOR` / `LOOP_OUTPUT` 不能作为可用网络能力 | 展开为静态图 / 自写 plugin |
| If 条件控制流 | `INetworkDefinition::addIfConditional()` 能返回非空 boundary object，但最小 If 网络 build 阶段失败；不能作为可用网络能力 | 展开为静态图 / 自写 plugin |
| ONNX `DynamicQuantizeLinear` | `supportsOperator("DynamicQuantizeLinear") == false`；这只指 ONNX parser op，不等于 Python `add_dynamic_quantize` layer API | 用静态 Q/DQ |
| Raw ONNX `Silu` | `supportsOperator("Silu") == false` | 如需 SiLU，用 `ActivationType::kSILU` 路径；当前 `ActivationType` 含 `kSILU` / `kHARD_SWISH` / `kMISH` |

注意：当前 parser 探针 `supportsOperator("If") == true`、`supportsOperator("Loop") == true` 不等于 engine build 路径可用；控制流最终以 `addIfConditional` / `addLoop` 最小 build 结果为准。

## 其他迁移语义变化

| 项 | 当前结论 |
|---|---|
| `FullyConnected` | 当前头文件不提供旧 `addFullyConnected` 路径；用 `MatrixMultiply` + `ElementWise` |
| Dims 位宽 | `Dims` 是 `Dims64`，维度字段为 `int64_t` |
| binding API | binding 下标 API 仍保留部分 deprecated wrapper；新代码优先用 Tensor API：`getTensorShape(name)` / `setTensorAddress(name, ptr)` / `setInputShape(name, dims)` + `enqueueV3(stream)` |
| 引擎可移植性 | `.engine` 按 IxRT 版本构建和加载；不要跨大版本复用 |
| data-dependent 输出 | `IOutputAllocator` / `getMaxOutputSize` 用法同 TRT，但 IxRT 要求空张量也返回非空 ptr（`size = std::max(size, 1)`），且回传内存 256 字节对齐 |

## 速记

当前版本不要再把 `IPluginV3`、`IEngineInspector`、ONNX parser `parse`、timing cache、`getTensorStrides`、C++ runtime temporary directory、常见 padding/resize/pooling/unary 限制写成不支持。仍需注意：无在线 calibrator / 无 refit 入口 / 无 DLA 执行路径 / 无 TF32·FP8·INT4·FP4 通用精度 / 非 LINEAR format 不可由用户稳定选择 / 无 If/Loop 控制流 / 无 `IStreamReader` 反序列化 / 无 version-compatible runtime / `FullyConnected` 旧接口已无。

内置插件清单见 [`builtin-plugins.md`](./builtin-plugins.md)。
