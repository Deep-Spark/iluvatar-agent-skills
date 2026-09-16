# ixjpeg-encode-image

`nvjpegEncodeImage` RGB / BGR / RGBI / BGRI 单图编码 demo。

## 签名

```c
nvjpegStatus_t nvjpegEncodeImage(
    nvjpegHandle_t, nvjpegEncoderState_t, const nvjpegEncoderParams_t,
    const nvjpegImage_t* source, nvjpegInputFormat_t input_format,
    int width, int height, cudaStream_t);
```

## 输入格式

仅接受 `nvjpegInputFormat_t`（`nvjpeg.h:132-135`）：`NVJPEG_INPUT_RGB=3 / BGR=4 / RGBI=5 / BGRI=6`。**不收 YUV/NV12**。

**`nvjpegImage_t` 布局**：

- 平面 RGB/BGR：`channel[0..2]` 三平面，pitch=w（连续）。
- 交错 RGBI/BGRI：`channel[0]` pitch=w\*3，`channel[1/2]=null`。

**子采样**：由 `nvjpegEncoderParamsSetSamplingFactors(params, css, stream)` 设（输出 jpg 内部子采样，与 RGB 输入无关）。本 demo 分别执行 `NVJPEG_CSS_420` 和 `NVJPEG_CSS_444`。

**取码流**：`nvjpegEncodeRetrieveBitstream(h, st, NULL, &len, stream)` 取长度，再 `(..., buf, &len, ...)` 取数据。

## 运行结果

逐项编码 RGB / BGR / RGBI / BGRI 输入，并写出 `enc_image_<css>_<format>.jpg`。

## 编译运行

```bash
bash build.sh      # Corex
bash build_nv.sh   # 原生 NV 对照
```
