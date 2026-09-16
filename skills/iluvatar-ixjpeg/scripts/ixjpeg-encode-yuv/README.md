# ixjpeg-encode-yuv

`nvjpegEncodeYUV` 单图 YUV / NV12 / NV21 编码 demo，覆盖全部 19 种输入组合。

## 签名

```c
nvjpegStatus_t nvjpegEncodeYUV(
    nvjpegHandle_t, nvjpegEncoderState_t, const nvjpegEncoderParams_t,
    const nvjpegImage_t* source,
    nvjpegChromaSubsampling_t chroma_subsampling,
    int width, int height, cudaStream_t);
```

## 输入格式

平面排布由编码前调 `IxjpegEncoderParamsSetFrameFormat(params, 0/1/2, stream)` 声明（默认 0=planar / 1=NV12 / 2=NV21）；子采样由 `chroma_subsampling` 参数直传。各组合的缓冲布局与色度平面大小见 SKILL.md 注意事项表格。

**`nvjpegImage_t` 布局**：

- planar：`channel[0]=Y(pitch W)`、`channel[1]=U(pitch cw)`、`channel[2]=V(pitch cw)`
- NV12：`channel[0]=Y(pitch W)`、`channel[1]=交错UV(pitch cw×2)`；需先 `IxjpegEncoderParamsSetFrameFormat(params, 1)`
- NV21：同 NV12，但 UV 顺序反转（VU）；`frame_format=2`

## 典型用法

```cpp
IxjpegEncoderParamsSetFrameFormat(params, 1, stream);  // 1=NV12，编码前设一次
nvjpegImage_t img{};
img.channel[0] = y;   img.pitch[0] = w;    // Y
img.channel[1] = uv;  img.pitch[1] = w;    // 交错 UV，pitch = w（非 w/2）
nvjpegEncodeYUV(h, st, params, &img, NVJPEG_CSS_420, w, h, stream);
// 取码流
size_t len = 0;
nvjpegEncodeRetrieveBitstream(h, st, nullptr, &len, stream);
nvjpegEncodeRetrieveBitstream(h, st, buf, &len, stream);
cudaStreamSynchronize(stream);
```

## 验证结果

Corex 共执行 19 个组合，支持矩阵见 SKILL.md。

planar 7 档在原生 NV 复验正确（`build_nv.sh`）；NV12/NV21 是 Corex 私有扩展，NV 上跳过。

## 编译运行

```bash
bash build.sh      # Corex
bash build_nv.sh   # 原生 NV 对照（planar 7 档）
```
