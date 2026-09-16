# ixjpeg-decode-image

`nvjpegDecode` 单图解码 demo。

## 签名

```c
nvjpegStatus_t nvjpegDecode(
    nvjpegHandle_t, nvjpegJpegState_t,
    const unsigned char* data, size_t length,
    nvjpegOutputFormat_t fmt, nvjpegImage_t* destination, cudaStream_t);
```

## 用法

调用顺序：`nvjpegCreateEx` → `nvjpegJpegStateCreate` → `nvjpegGetImageInfo`（查宽高/通道数）→ 分配输出缓冲 → `nvjpegDecode`。

`-fmt` 当前验证格式（默认 `rgb`）：`rgb` / `bgr` / `rgbi` / `bgri` / `yuv` / `unchanged` / `y` / `nv12`，Corex 另支持 `nv21`。Corex 无 `yuy2` / `unchangedi_u16` 枚举，编译期排除；NV 对照保留 `nv12` / `yuy2` / `unchangedi_u16`。`ixjpeg-decode-check` 对 Corex 的 9 种输出格式执行 sentinel 写入检查，并逐字节验证 NV12/NV21 的 Y 与色度排布。

`-o output_dir` 将解码结果写成 BMP（目录不存在会自动创建），仅支持 rgb/bgr/rgbi/bgri 格式；其他格式传 `-o` 会报错退出。

**Corex `nvjpegGetImageInfo` 陷阱**：只填 `ws[0]/hs[0]`，`ws[1..3]/hs[1..3]` 全为 0。分配 yuv/unchanged chroma 缓冲不能依赖这些值，需从 `chroma_subsampling` 枚举手动推算每个平面尺寸。

## 编译运行

```bash
bash build.sh      # Corex
bash build_nv.sh   # 原生 NV 对照
```

测试图片：`../../assets/images/`（baseline JPEG）。Progressive JPEG 不支持，隔离在 `assets/images/progressive/`。
