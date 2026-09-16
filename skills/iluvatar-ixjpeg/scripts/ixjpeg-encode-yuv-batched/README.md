# ixjpeg-encode-yuv-batched

`IxjpegEncodeYUVBatched` YUV / NV12 / NV21 批量编码 demo（Corex 私有扩展），每个组合一次提交 3 帧。

## 签名

```c
ixjpegStatus_t IxjpegEncodeYUVBatched(
    ixjpegHandle_t, ixjpegEncoderState_t,
    const unsigned char* SrcDevBuffer,
    const jpegEncParam* EncParams, int BatchSize, ixStream_t);
```

## 输入格式

一块连续 device 缓冲 `SrcDevBuffer`，含 `BatchSize` 帧依次排布；每帧一个 `jpegEncParam`：

```c
jpegEncParam {
    unsigned int width;
    unsigned int height;
    unsigned int frame_format;   // 0=planar(I420) / 1=NV12 / 2=NV21
    unsigned int frame_rate;
    unsigned int image_format;   // nvjpegChromaSubsampling_t 枚举值，见 SKILL.md
    unsigned int buffer_size;    // 单帧字节数
    unsigned int quality;
    char         savepath[256];
}
```

**`image_format` = `nvjpegChromaSubsampling_t` 枚举值**（见 SKILL.md）。

**取码流**：

```c
IxjpegEncodeRetrieveBitstreamBatched(h, st, NULL, lengths, batch, stream);
IxjpegEncodeRetrieveBitstreamBatched(h, st, data, NULL, batch, stream);
```

## 验证结果

Corex 共执行 19 个组合，每个组合用一块连续 device 缓冲提交 3 帧，并分别写为
`enc_batched_<format>-b0/b1/b2.jpg`。支持矩阵见 SKILL.md。

此 API 为 Corex 私有扩展，无 NV 对照（`build_nv.sh` 不适用）。

## 编译运行

```bash
bash build.sh
```
