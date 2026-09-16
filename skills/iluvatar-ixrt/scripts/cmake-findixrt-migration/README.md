# FindTensorRT -> FindIxRT 迁移模式

适用场景：原工程已有 `cmake/FindTensorRT.cmake`，并且内部统一链接
`TensorRT::TensorRT` / `TensorRT::nvonnxparser`，现在要加 Corex IxRT 构建路径。

核心原则：**不要把 `ixrt` 库名塞进原 `FindTensorRT.cmake`**。保留 NVIDIA 路径只找
`nvinfer` / `nvonnxparser`，另写 `FindIxRT.cmake`，但让它输出同一组 TensorRT 兼容变量和
imported targets。这样上层 CMake 和源码不用分叉，NV 默认构建路径也不会被 Corex 默认路径污染。

## 构建 & 运行

```bash
bash build.sh     # Corex/IxRT：FindIxRT.cmake 生成 TensorRT::TensorRT 兼容 target
bash build_nv.sh  # NVIDIA/TensorRT：同一份 CMakeLists 走 FindTensorRT.cmake
```

`cmake-findixrt-migration` 只做一件事：通过 `FindIxRT.cmake` 创建的 `TensorRT::TensorRT` target 链到
`libixrt.so`（或通过 `FindTensorRT.cmake` 链到 `libnvinfer.so`），然后调用
`nvinfer1::createInferRuntime()`。成功时打印：

```text
CMake FindIxRT migration: TensorRT::TensorRT target runtime OK
```

## 顶层 CMake 改法

```cmake
option(TRT_CPP_API_USE_IXRT "Build against Corex IxRT instead of NVIDIA TensorRT" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(CUDAToolkit REQUIRED)

if(TRT_CPP_API_USE_IXRT)
    find_package(IxRT REQUIRED)
    set(TRT_CPP_API_WITH_IXRT ON)
else()
    find_package(TensorRT REQUIRED)
    set(TRT_CPP_API_WITH_IXRT OFF)
endif()

target_link_libraries(my_target PRIVATE TensorRT::TensorRT CUDA::cudart)
```

如果用 `CMAKE_CUDA_COMPILER_ID` 判断平台，`project()` 必须启用 CUDA language：

```cmake
project(my_project LANGUAGES CXX CUDA)

if(CMAKE_CUDA_COMPILER_ID STREQUAL "ILUVATAR")
    find_package(IxRT REQUIRED)
else()
    find_package(TensorRT REQUIRED)
endif()
```

否则 `CMAKE_CUDA_COMPILER_ID` 为空，Corex 构建会误走 `FindTensorRT.cmake`。

`TRT_CPP_API_WITH_IXRT` 建议写入生成配置头，用于源码里门控 IxRT 缺失的 TensorRT API：

```cmake
#cmakedefine01 TRT_CPP_API_WITH_IXRT
```

## FindIxRT 输出什么

`find_package(IxRT)` 必然会产生 `IxRT_FOUND`；除此之外，业务侧只消费 TensorRT 兼容变量：

```cmake
TensorRT_INCLUDE_DIR
TensorRT_nvinfer_LIBRARY        # 指向 libixrt.so
TensorRT_nvonnxparser_LIBRARY   # 指向 libixrtonnxparser.so
TensorRT_VERSION
TensorRT_VERSION_MAJOR
TensorRT_VERSION_MINOR
TensorRT_VERSION_PATCH
TensorRT_LIBRARIES
TensorRT_INCLUDE_DIRS
```

并创建兼容 targets：

```cmake
TensorRT::TensorRT
TensorRT::nvonnxparser
```

不建议额外引入 `IxRT::IxRT` 或让业务 target 同时支持两套 target 名；否则下游 CMake 会到处出现
`if(Corex)` 分支。

## Corex 构建脚本

真实工程若不使用默认安装路径，可在 `build_corex.sh` 按下面方式显式注入路径；本 case 的
`build.sh` 依赖 `FindIxRT.cmake` 的默认 `/usr/local/corex` 搜索路径，是更短的最小可跑版本。

```bash
export CUDA_HOME=/usr/local/corex
export COREX_HOME="$CUDA_HOME"
export TensorRT_ROOT="$COREX_HOME"
export CMAKE_CUDA_ARCHITECTURES=ivcore11
export LIBRARY_PATH="$COREX_HOME/lib64${LIBRARY_PATH:+:$LIBRARY_PATH}"
export LD_LIBRARY_PATH="$COREX_HOME/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

cmake -S . -B build-corex \
  -DCMAKE_CXX_COMPILER="$COREX_HOME/bin/clang++" \
  -DCMAKE_CUDA_COMPILER="$COREX_HOME/bin/clang++" \
  -DCMAKE_CUDA_ARCHITECTURES="$CMAKE_CUDA_ARCHITECTURES" \
  -DCMAKE_CUDA_FLAGS="-std=c++20" \
  -DCUDAToolkit_ROOT="$CUDA_HOME" \
  -DTRT_CPP_API_USE_IXRT=ON \
  -DTensorRT_INCLUDE_DIR="$TensorRT_ROOT/include" \
  -DTensorRT_nvinfer_LIBRARY="$TensorRT_ROOT/lib64/libixrt.so" \
  -DTensorRT_nvonnxparser_LIBRARY="$TensorRT_ROOT/lib64/libixrtonnxparser.so"

cmake --build build-corex -j"$(nproc)"
```

注意：Corex CMake CUDA language 应使用 `/usr/local/corex/bin/clang++`。`/usr/local/corex/bin/nvcc`
是兼容包装/脚本，可能导致新版 CMake 在 CUDA compiler implicit link info 探测阶段失败。

## 源码门控

IxRT 版本宏是 `NV_TENSORRT_MAJOR=1`，不能用 `NV_TENSORRT_MAJOR < 11` 推断它支持 TensorRT 10 的
legacy API。典型门控写法：

```cpp
#if TRT_CPP_API_TENSORRT_VERSION_MAJOR < 11 && !TRT_CPP_API_WITH_IXRT
// IInt8EntropyCalibrator2 / setInt8Calibrator 等 legacy calibrator 路径
#endif
```

常见需要门控为 unsupported 的 API：

- `IInt8EntropyCalibrator2` / `IBuilderConfig::setInt8Calibrator`
- `BuilderFlag::kVERSION_COMPATIBLE`
- `IBuilderConfig::setHardwareCompatibilityLevel`
- DLA 相关：`setDefaultDeviceType` / `setDLACore` / `BuilderFlag::kGPU_FALLBACK`
- `IBuilderConfig::setProfileStream`
- `IPluginRegistry::loadLibrary`（当前 IxRT 头无该成员；自定义插件通过链接插件库并 `registerCreator` 暴露 creator）
