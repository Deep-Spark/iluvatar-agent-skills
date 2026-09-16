# ixjpeg-encode-pipeline

`IxjpegDoEncoderPipeline` 单帧 pipeline 编码 demo（Corex 私有扩展）。

## 签名

```c
ixjpegStatus_t IxjpegDoEncoderPipeline(
    IxJpegEnc hEncoder, const void* Buffer,
    const jpegEncParam* EncParams, jpegEncOutPut* OutBuff,
    unsigned int Num, bool SaveFile);
```

## 输入格式

**`frame_format`**：

| 值 | 含义 |
|----|------|
| 0 | planar（I420，Y/U/V 三平面） |
| 1 | NV12（Y + UV 交错） |
| 2 | NV21（Y + VU 交错） |

**`image_format`**

| 值 | 子采样 |
|----|--------|
| 0 | 420 |
| 1 | 422 |
| 2 | 440 |
| 3 | 444 |
| 4 | GRAY（400） |

`NVJPEG_CSS_411` / `NVJPEG_CSS_410` 在 `FrameFormat` 里无对应值，**不支持**。


## 输出

直接写进 `jpegEncOutPut`，无需单独 retrieve：

```c
typedef struct jpegEncOutPut_t {
    void*        jpegBuff;
    unsigned int buffer_size;
} jpegEncOutPut;
```

输出由库分配，使用后调用
`IxjpegEncoderOutPutRelease(hEncoder, &out)`。`SaveFile=true` 时另存到
`EncCfg.savepath`。

## 典型用法

```cpp
IxJpegEnc encoder = nullptr;
IxjpegCreateEncoderHandle(&encoder);

jpegEncOutPut out{};
IxjpegDoEncoderPipeline(encoder, dBuf, &EncCfg, &out, 1, false);
// 使用 out.jpegBuff / out.buffer_size。
IxjpegEncoderOutPutRelease(encoder, &out);
IxjpegDestroyEncoderHandle(encoder);
```

## 验证结果

当前实测 planar/NV12/NV21 × 420/422/440/444 + planar-GRAY 共 13 个组合
全部生成非空 JPEG。

此 API 为 Corex 私有扩展，无 NV 对照。

## 编译运行

```bash
bash build.sh
```
