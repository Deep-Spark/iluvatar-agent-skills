---
name: iluvatar-ixcodec
description: >-
  Iluvatar Corex ixvid*/libnvcuvid 视频解码与 IxEnc*/libnvencode 视频编码迁移参考。
  覆盖 H.264/HEVC/VP9 parser callback、独立 map 线程、PTS/EOS、I420/NV12/NV21、
  显示区域裁剪、硬件缩放、device resource register/map、SPS/PPS/VPS 和 mux。
  MPEG-4 Part 2 解码不支持。
  编码支持 B-frame、multiref、custom-GOP hierarchical temporal layer、HEVC
  max-merge、strong-intrasmoothing 和 HEVC lossless；不支持 H.264 lossless、
  lookahead、field encoding、质量/速度 preset、alpha、AV1 和 YUV444 encoding。
  MR100 不支持硬件编码器，编码能力仅适用于 KC-V100X。覆盖 video pipeline 的
  graph runtime、推理、GPU OSD、跟踪、Triton、推流、录像和 JPEG 快照。用于迁移
  NVIDIA NVDEC/NVENC、构建视频分析流水线，以及排查解码阻塞、颜色布局、硬件转码
  和编码能力问题。
---

# IxCodec：Corex 视频硬件编解码

MR100 不支持硬件编码器；编码能力仅适用于 KC-V100X。解码能力需按实际卡型和码流复核。

## 读取入口

| 问题方向 | 必须读取 |
|---|---|
| decoder、parser、直喂、map、PTS/EOS、输出布局、缩放、解码能力 | [解码参考](references/decoder.md) |
| encoder、resource map、SPS/PPS、GOP、B-frame、reference、lossless、编码格式与尺寸 | [编码参考](references/encoder.md) |
| 解码后直接编码、硬件转码、demux/mux 完整数据流 | 同时读取[解码参考](references/decoder.md)和[编码参考](references/encoder.md) |
| 完整工程、graph runtime、推理、GPU OSD、跟踪、Triton、推流、录像和 JPEG 快照 | [完整视频分析流水线](references/pipeline.md) |

## 使用规则

1. 务必先阅读 `trt_yolo_video_pipeline` 源码，选择最符合目标场景的样例并实际跑通，再开始迁移或改写；入口见[完整视频分析流水线](references/pipeline.md)。
2. 读取对应 reference，确认当前 API 差异、线程模型和能力边界。
