# cmake-corex-ixrt-template

在 Iluvatar Corex 上构建**用 TensorRT 的工程**的标准模板。一个自定义插件（NonZero）+ 一个 `.cu` kernel，覆盖 plugin / CUDA kernel / FP16 路径这些真实场景。核心是把 `nvinfer` → corex 的 **IxRT** 映射好。

## 构建 & 运行

```bash
bash build.sh     # corex/IxRT：配置 + 编译(.cpp + .cu) + 自动跑、自校验
bash build_nv.sh  # 同一份 CMakeLists 在原生 NVIDIA + TensorRT 上构建
```

`sample_non_zero_plugin` 自带数据与校验：构建 engine → 在随机矩阵上跑 NonZero 插件 → host 侧核对「报告的每个索引都非零、且所有非零都被报告」，最后打印 `PASS: NonZero plugin reported all N non-zero elements correctly.`。可选参数 `--columnOrder`（列主序输出）、`--fp16`（插入 FP16 cast）。

---

## 模板的三个关键点

### 1. `find_package(CUDAToolkit)` 一把解决 include + cudart

详见 [`cmake-corex-template`](../../../iluvatar-cuda-base/scripts/cmake-corex-template/README.md)。

### 2. TensorRT 库统一由构建脚本注入 `-DTensorRT_LIBRARIES`

```cmake
target_link_libraries(sample_non_zero_plugin ${TensorRT_LIBRARIES} CUDA::cudart)
```

CMakeLists 不写任何默认值，库名**完全由脚本提供**，两平台都用**裸库名**（对称）。`TensorRT_LIBRARIES` 是「核心库 + plugin 库」的分号列表：

| 平台 | 脚本传入（`TensorRT_LIBRARIES`） | 链接器怎么找 |
|---|---|---|
| corex | `-DTensorRT_LIBRARIES="ixrt;ixrt_plugin"` + `export LIBRARY_PATH=/usr/local/corex/lib64` | 裸名 → `-lixrt -lixrt_plugin`；两库都在 `/usr/local/corex/lib64`，不在 ld 默认路径，靠 `LIBRARY_PATH` 补链接期搜索目录 |
| 原生 NV | `-DTensorRT_LIBRARIES="nvinfer;nvinfer_plugin"` | 裸名 → `-lnvinfer -lnvinfer_plugin`，两库已在 ld 默认路径，无需补 |

头文件两平台都显式传 `-DTensorRT_INCLUDE_DIR`：corex `/usr/local/corex/include`，NV `/usr/include/x86_64-linux-gnu`（deb 包 multiarch 路径）。CMakeLists 无条件 `include_directories(${TensorRT_INCLUDE_DIR})`，两边对称、不依赖隐式回退。变量名沿用 NVIDIA `FindTensorRT.cmake` 的约定，方便将来切到 `find_package(TensorRT)`——注意当前 NV/corex 都不发 `TensorRTConfig.cmake`，`find_package(TensorRT)` 现在是找不到的（`TensorRT_FOUND=0`），所以才走 `-D` 注入。

### 3. `.cu` kernel 必须把 CUDA 设成 enabled language

```cmake
project(sample_non_zero_plugin LANGUAGES CXX CUDA)
```

本例有 `nonZeroKernel.cu`，所以 `project()` 必须带 `CUDA`（或 `enable_language(CUDA)`），且 `build.sh` 设 `CMAKE_CUDA_ARCHITECTURES=ivcore11`，否则 corex clang++ 不知道目标 arch。纯 host 的 TRT 工程（无 `.cu`）此项是惰性的。

## `build.sh` 的完整 corex 配置

```bash
export CUDA_HOME=/usr/local/corex
export TensorRT_ROOT=$CUDA_HOME
export CMAKE_CUDA_ARCHITECTURES=ivcore11
export LIBRARY_PATH=$CUDA_HOME/lib64
cmake -S . -B build \
  -DTensorRT_INCLUDE_DIR="$TensorRT_ROOT/include" \
  -DTensorRT_LIBRARY=ixrt \
  -DTensorRT_LIBRARIES="ixrt;ixrt_plugin" \
  -DTensorRT_Plugin_INCLUDE_DIR="$TensorRT_ROOT/include" \
  -DTensorRT_Plugin_LIBRARY=ixrt_plugin
```

`build_nv.sh` 对称地传同样 5 个变量（值换成 `nvinfer` / `nvinfer_plugin` / multiarch 头路径）。CMakeLists 实际用到的是 `TensorRT_INCLUDE_DIR` + `TensorRT_LIBRARIES`（后者一次链核心库 + plugin 库）；单数的 `TensorRT_LIBRARY` / `TensorRT_Plugin_LIBRARY` / `TensorRT_Plugin_INCLUDE_DIR` 当前没用到（cmake 会提示 `unused variable`），保留是便于套用到更复杂的工程。

迁移原则、CMake 与构建脚本的职责边界见
[`references/cmake-migration.md`](../../references/cmake-migration.md)。
