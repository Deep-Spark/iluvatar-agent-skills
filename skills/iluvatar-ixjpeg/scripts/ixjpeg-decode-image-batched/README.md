# ixjpeg-decode-image-batched

`nvjpegDecodeBatched` 批量解码 demo。

## 签名

```c
nvjpegStatus_t nvjpegDecodeBatchedInitialize(
    nvjpegHandle_t, nvjpegJpegState_t,
    int batch_size, int nThreads, nvjpegOutputFormat_t fmt);

nvjpegStatus_t nvjpegDecodeBatched(
    nvjpegHandle_t, nvjpegJpegState_t,
    const unsigned char* const* data, const size_t* lengths,
    nvjpegImage_t* destinations, cudaStream_t);
```

每批解码前必须先调 `nvjpegDecodeBatchedInitialize`（单图 `nvjpegDecode` 无此步）。

Corex 的同一 `nvjpegJpegState_t` 再次 initialize 时不能切换输出格式：调用返回 SUCCESS，但继续沿用第一次初始化的格式。输出格式发生变化时，销毁并重建 state 后再 initialize。

## 用法

调用顺序：`nvjpegCreateEx` → `nvjpegJpegStateCreate` → `nvjpegGetImageInfo`（查宽高/通道数）→ 分配输出缓冲 → `nvjpegDecodeBatchedInitialize` → `nvjpegDecodeBatched`。

handle 用 `nvjpegCreateEx(NVJPEG_BACKEND_GPU_HYBRID, ...)`；若返回 `NVJPEG_STATUS_ARCH_MISMATCH` 则自动降级 `NVJPEG_BACKEND_DEFAULT`。当前头文件声明 `NVJPEG_BACKEND_HARDWARE`，但本 demo 和推荐迁移路径不依赖该 backend。

**NV 侧**在 `hw_decode_available` 分支用 `nvjpegJpegStreamParseHeader` + `nvjpegDecodeBatchedSupported` 逐帧查询是否支持（`#ifndef __ILUVATAR__` 包裹）；Corex 无这两个 API，编译时该块被跳过，整批直接入队。

`-fmt` 支持格式（默认 `rgb`）：`rgb` / `bgr` / `rgbi` / `bgri` / `yuv` / `unchanged` / `y` / `nv12`，Corex 另支持 `nv21`。Corex 无 `yuy2` / `unchangedi_u16` 枚举，编译期排除；NV 对照保留 `nv12` / `yuy2` / `unchangedi_u16`。

`-o output_dir` 将解码结果写成 BMP，仅支持 rgb/bgr/rgbi/bgri；其他格式传 `-o` 会报错退出。

**Corex `nvjpegGetImageInfo` 陷阱**：只填 `ws[0]/hs[0]`，`ws[1..3]/hs[1..3]` 全为 0。分配 yuv/unchanged chroma 缓冲不能依赖这些值，需从 `chroma_subsampling` 枚举手动推算每个平面尺寸。

## 编译运行

```bash
bash build.sh      # Corex（含 -D__ILUVATAR__）
bash build_nv.sh   # 原生 NV 对照（保留完整 nvjpegJpegStreamParseHeader + nvjpegDecodeBatchedSupported 流程）
```

测试图片：`build.sh` 和 `build_nv.sh` 都会从 `../../assets/images/` 复制固定的 4 张 SOF0 baseline JPEG 到 `build/baseline-inputs/`。Corex 以 batch=4 分别执行 rgb/bgr/rgbi/bgri/yuv/unchanged/y/nv12/nv21；正确性判断放在 `ixjpeg-decode-check/`。
