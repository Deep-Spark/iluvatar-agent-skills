# IxCodec 编码

## 索引

| 主题 | 内容 |
|---|---|
| [硬件与 API](#硬件与-api) | 支持卡型、头文件和链接库 |
| [编码流程](#编码流程) | session、resource、submit/lock 和 EOS |
| [编码注意事项](#编码注意事项) | 线程、device 输入、SPS/PPS 和 timing |
| [编码能力](#编码能力) | GOP、reference、HEVC 专项和负能力 |
| [格式与尺寸](#格式与尺寸) | profile、bit depth、chroma 和尺寸 |
| [验证用例](#验证用例) | capability 与 feature probes |
| [参考实现](#参考实现) | 完整编码和 mux 数据流 |

## 硬件与 API

MR100 不支持硬件编码器；以下编码能力仅适用于 KC-V100X。

| 项目 | 原生接口 | 兼容接口 |
|---|---|---|
| 头文件 | `<IX/ixcodec/ixEncodeAPI.h>` | `<nvEncodeAPI.h>` |
| API | `IxEnc*` | `NvEnc*`、`nvEnc*` |
| 链接库 | `libnvencode.so` | `libnvencode.so` |

## 编码流程

1. 调用 `IxEncGetEncodePresetConfigure` 取得 codec 默认配置，再填写 GOP、QP、timing 等字段。
2. 用有效 context 打开 session，获取 `IxEncGetFrameSize`。
3. device 输入先 `IxEncRegisterResource`，再 `IxEncMapInputResource`；encode 接收 mapped resource。
4. 提交输入后轮询 `IxEncLockBitstream`；返回地址位于 device，复制到 host 后 unlock。
5. EOS 后继续 lock，直到 `encStatus_finish`。

- 参考代码：[初始化与 resource map](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/codec/IxEncoder.cpp#L55) · [device 输入拷贝](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/codec/IxEncoder.cpp#L390) · [lock/copy/unlock](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/codec/IxEncoder.cpp#L308)

## 编码注意事项

### Session 线程模型

当前探针和参考实现都在同一线程内完成 submit/lock。当前接口未给出同一 session 的并发调用
保证，不要在未单独验证的情况下跨线程共享 encoder session。

### Device 输入资源

Corex 的 `IX_ENC_REGISTER_RESOURCE` 只有 `resourceToRegister` 和输出的
`registeredResource`，不接收 width、height、pitch 或 buffer format；编码尺寸和输入格式在
session 初始化时确定。不要照搬 NVIDIA NVENC 在注册结构中填写 surface 几何和格式的代码。

被注册的 device 缓冲必须符合 session 初始化时确定的布局。参考实现分配编码尺寸的紧凑
缓冲并注册、map 一次：紧凑 NV12 每帧连续 D2D，带 pitch 或 Y/UV 分离的输入按 plane
做二维拷贝；提交编码时传 `mappedResource`，不是裸 device 指针。

- 参考代码：[注册并 map 自有缓冲](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/codec/IxEncoder.cpp#L95-L116) · [紧凑与带 pitch 输入拷贝](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/codec/IxEncoder.cpp#L405-L451)

### SPS/PPS 与 mux

当前编码接口没有 sequence-parameter 获取 API。SPS/PPS/VPS 与 IDR slice 可能分散在多个
lock 输出中；累积首个 access unit，提取 extradata 后再调用 `avformat_write_header`。

- 参考代码：[提取参数集](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/codec/IxEncoder.cpp#L154) · [累积首个 AU](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/codec/IxEncoder.cpp#L329) · [延迟写 mux header](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/codec/Enmux.cpp#L41)

### Timing 字段

H.264 使用 `numUnitsInTick=1`、`timeScale=2*fps`；HEVC 使用
`numUnitsInTick=1`、`timeScale=fps`、`numTicksPocDiffOne=1`。字段不完整会生成无效 VUI timing。

## 编码能力

| 能力 | 当前结论 |
|---|---|
| H.264/HEVC B-frame 与 B GOP preset | 支持 |
| H.264/HEVC multi-reference | 支持 |
| [hierarchical temporal layer](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/nvenc-video-encoder-api-prog-guide/index.html#hierarchical-b-frame-mode) | 支持 custom GOP，实测 temporal ID 0/1/2 |
| HEVC max-merge | 支持 `maxNumMerge=1/2` |
| HEVC strong-intrasmoothing | 支持开关 |
| HEVC lossless | 支持，解码结果与输入逐字节一致 |
| H.264 lossless | 不支持，AVC 参数没有 lossless 配置字段 |
| [lookahead](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/nvenc-video-encoder-api-prog-guide/index.html#lookahead-level) | 不支持 |
| [H.264 interlaced/field encoding](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.0/read-me/index.html) | 不支持，只输出 progressive |
| [质量/速度 preset](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/nvenc-video-encoder-api-prog-guide/index.html#selecting-encoder-preset-configuration) | 不支持；`gopPresetIdx` 只选择 GOP 结构 |
| [alpha encoding](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/nvenc-video-encoder-api-prog-guide/index.html#alpha-layer-encoding-support-in-hevc) | 不支持 |
| AV1 / YUV444 encoding | 不支持 |
| encoder reconfigure | API 已声明且 function-list 指针存在，但头文件明确标注尚不支持 |

标准 `RA_IB` 只输出 temporal ID 0；hierarchical temporal layer 需要 custom GOP 的
`temporalId` 配置。编码侧 `bitFormat=13/14/15` 即使 encode 返回 success，也只得到 0 字节输出。
完整证据见 [encoder feature probe](../scripts/corex-encoder-feature-probe/README.md)。

## 格式与尺寸

- H.264 4:2:0 8-bit 支持 High、Baseline、Main、Extended profile。
- HEVC 支持 4:2:0 8-bit Main 和 4:2:0 10-bit Main10。
- NV16/P210 4:2:2 可作为输入 layout，但输出仍是 `yuv420p` / `yuv420p10le`。
- `IX_ENC_SRC_FORMAT` 没有 YUV444 source format。
- 已验证 `256x128`、`264x128`、`256x136`、`8192x8192`。这证明宽或高不要求
  16 对齐，但不外推任意 8 对齐尺寸都支持。

完整证据见 [encoder capability probe](../scripts/corex-encoder-capability/README.md)。

## 验证用例

| 用例 | 内容 | 执行 |
|---|---|---|
| [corex-encoder-capability](../scripts/corex-encoder-capability/) | profile、输入格式、尺寸 | `bash build.sh && bash run.sh` |
| [corex-encoder-feature-probe](../scripts/corex-encoder-feature-probe/) | GOP、B-frame、multiref、hierarchical、HEVC 专项与负能力 | `bash build.sh && bash run.sh` |

两个 `run.sh` 都不接收参数，一次性执行各自的全部验证。输入素材由
[`assets/videos/generate.sh`](../assets/videos/generate.sh) 按需生成。

## 参考实现

### [trt_yolo_video_pipeline：硬件编码与 mux 数据流](https://github.com/121786404/trt_yolo_video_pipeline/tree/iluvatar)

| 类 / 文件 | 职责 |
|---|---|
| [`IxEncoder`](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/codec/IxEncoder.cpp) | mapped device 输入、submit/lock、参数集提取和 timing |
| [`Enmuxer`](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/codec/Enmux.cpp) | 等 extradata 就绪后写 header，再写 MP4/FLV/RTSP packet |
