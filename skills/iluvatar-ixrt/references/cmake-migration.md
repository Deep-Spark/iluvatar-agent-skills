# TensorRT → IxRT 的 CMake 迁移

本页记录 TensorRT 工程迁到 IxRT 时，`CMakeLists.txt`、查找模块和构建脚本各自应承担
什么职责。可直接运行的最小模板见
[`scripts/cmake-corex-ixrt-template/`](../scripts/cmake-corex-ixrt-template/)；已有
`FindTensorRT.cmake` 的工程见
[`scripts/cmake-findixrt-migration/`](../scripts/cmake-findixrt-migration/)。

## 迁移原则

- 头文件和库路径显式注入；用 `find_package(CUDAToolkit)` 解析 cudart。
- Corex 库名显式写为 `ixrt`、`ixrt_plugin`、`ixrtonnxparser`，不要让链接结果依赖
  构建目录中的临时文件。
- 需要兼容现有下游时，由 `FindIxRT.cmake` 输出 `TensorRT::TensorRT` 等兼容 target；
  不修改 NVIDIA 默认查找路径。
- `build_corex.sh` 只注入 Corex 工具链、`ivcore11`、`CUDAToolkit_ROOT`、IxRT
  include/lib 路径和必要 CMake 变量；库映射与 target 关系留在 CMake 配置中。

## 禁止用软链接伪装 TensorRT 库

不要在构建脚本中生成以下临时映射：

```text
libnvinfer.so        -> libixrt.so
libnvinfer_plugin.so -> libixrt_plugin.so
libnvonnxparser.so   -> libixrtonnxparser.so
```

这种做法会把迁移关系隐藏在脚本副作用里，使链接命令、RPATH、运行时加载和打包结果无法
从 CMake 配置直接审计。正确做法是把库名替换显式写进 `CMakeLists.txt`、
`FindIxRT.cmake` 或工程已有的 TensorRT 查找模块。

## 两类模板

| 场景 | 使用 |
|---|---|
| 新建或可直接修改的工程 | `cmake-corex-ixrt-template/`：同一份 CMakeLists，由两平台脚本注入不同库名 |
| 已有 `FindTensorRT.cmake` 的工程 | `cmake-findixrt-migration/`：新增 `FindIxRT.cmake`，继续输出 TensorRT 兼容变量和 targets |

模板只证明对应最小 plugin/kernel 工程的配置与链接方式可用。业务工程仍需检查自定义
plugin、parser、RPATH、安装规则和打包目录，请自行确认适用性。
