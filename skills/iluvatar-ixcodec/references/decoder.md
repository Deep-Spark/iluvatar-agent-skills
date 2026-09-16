# IxCodec 解码

## 索引

| 主题 | 内容 |
|---|---|
| [API 差异](#api-差异) | create、timestamp、format callback、map |
| [解码流程](#解码流程) | Annex-B、parser、直喂和线程模型 |
| [解码注意事项](#解码注意事项) | PTS、布局、缩放、EOS 和重连 |
| [解码能力](#解码能力) | codec、profile、chroma、尺寸和 caps |
| [验证用例](#验证用例) | decoder capability probe |
| [参考实现](#参考实现) | 完整项目和 SDK 风格实现 |

## API 差异

| 项目 | Corex | NVIDIA NVDEC |
|---|---|---|
| 头文件 / API | `<IX/ixcodec/ixviddec.h>` / `ixvid*`；兼容接口为 `<nvviddec.h>` / `cuvid*` | `<nvcuvid.h>` / `cuvid*` |
| 创建 decoder | `ixvidCreateDecoder(context, &decoder, &config)`；兼容接口为 `cuvidCreateDecoder(context, &decoder, &config)` | `cuvidCreateDecoder(&decoder, &config)` |
| source packet 时间戳 | `IXVIDSOURCEDATAPACKET` 没有 timestamp 字段 | packet 可携带 timestamp |
| display 时间戳 | `IXVIDPARSERDISPINFO` 没有 timestamp 字段 | display info 可返回 timestamp |
| sequence callback 格式 | `IXVIDEOFORMAT` 仅含 `int i` 占位字段，不提供流格式 | `CUVIDEOFORMAT` 提供完整格式 |
| 实际流格式 | decoder 的 `pOnStreamChanged(IXVIDFormat*)` | sequence callback |
| map | `ixvidMapVideoFrame(decoder, &ptr, &pitch, &params)`，没有 picture index | 按 display callback 的 picture index map |
| 链接库 | `libnvcuvid.so` | `libnvcuvid.so` |

Corex parser 可用，但 display callback 不能直接照搬 NVIDIA 的 map 逻辑。Corex 的原生和
兼容 create API 都增加了显式 `CUcontext` 第一参数；迁移 NVIDIA 代码时，保存有效 context，
并把它的 handle 传给 create API。NVIDIA 的两参数调用不能原样保留。

**完整用法：[创建并持有 CUDA context，再创建 decoder/parser](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/codec/IxDecoder.cpp#L45-L122)**

## 解码流程

H.264/HEVC Annex-B 和 VP9 frame payload 推荐使用以下流程。

MP4/MKV 等封装中的 H.264/HEVC packet 可能是长度前缀的 AVCC/hvcC 格式。检测到
configuration record 时，先经过 FFmpeg `h264_mp4toannexb` / `hevc_mp4toannexb` BSF，
再交给 parser；裸流或已经是 Annex-B 的输入可以直接提交。

- 参考代码：[检测 AVCC/hvcC 并创建 BSF](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/node/FFmpegReadNode.cpp#L229-L250) · [提交 BSF 输出](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/node/FFmpegReadNode.cpp#L111-L125)

1. 用有效 `CUcontext` 创建 decoder，注册 `pOnStreamChanged`。
2. 创建 parser；sequence callback 返回成功，decode callback 调用 `ixvidDecodePicture`。
3. demux 线程持续调用 `ixvidParseVideoData`。
4. 独立 map 线程循环调用 `ixvidMapVideoFrame`，拷贝已就绪帧后立即 unmap。
5. 输入流真正结束且不再复用 decoder 时提交 EOS，并继续 map，直到全部已提交 picture 输出完毕。
6. 关闭时先通知线程停止，再销毁 decoder 解除阻塞，最后 join map 线程。

- 参考代码：[创建 decoder/parser 与启动 map 线程](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/codec/IxDecoder.cpp#L62) · [parser callbacks](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/codec/IxDecoder.cpp#L188) · [map_loop](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/codec/IxDecoder.cpp#L248) · [EOS 与 drain](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/codec/IxDecoder.cpp#L403)

探针也保留直喂入口：把完整 picture 填入 `IXVIDPICPARAMS` 后调用
`ixvidDecodePicture`，decoder 使用 `bitstreamMode=2`。直喂要求应用自己完成 access-unit
拆分；不能把任意 demux packet 都当成完整 picture。

两类入口使用的 codec 数值不同：

| Codec | parser `CodecType` | decoder `bitFormat` |
|---|---:|---:|
| H.264 | 4 | 0 |
| HEVC | 8 | 12 |
| VP9 | 10 | 13 |

## 解码注意事项

### 输入与输出必须解耦

`ixvidDecodePicture` 返回 success 只表示 picture 已被接收，不表示已有显示帧可 map。
B-frame、重排序和内部缓存会造成输出延迟；部分 HEVC 码流需要后续 picture 或 EOS 才输出。
不要在喂包线程中紧跟一个阻塞式 map，否则后续输入和 EOS 无法继续提交。

`ixvidMapVideoFrame` 在无输出时可能阻塞。map 线程必须设置正确的 CUDA context，并使用有界
输出队列，避免下游变慢时无限占用显存。

### PTS 由应用维护

Corex parser source packet 和 display info 都没有 timestamp 字段，parser 路径的 map
实测返回 timestamp 0，不能自动回传输入 PTS。应用应在 access-unit 边界保存输入 PTS，并在
map 输出的显示顺序中恢复。若一个 demux packet 含多个 picture，或一个 picture 跨多个
packet，先完成 access-unit 拆分，不能假设 packet 与 decode callback 永远一一对应。

直喂路径不同：写入 `IXVIDPICPARAMS.timestamp` 的值可由 map 返回。不要把直喂和 parser
路径的 timestamp 行为混为一谈。

- 参考代码：[保存 parser PTS](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/codec/IxDecoder.cpp#L371) · [按显示顺序设置输出 PTS](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/codec/IxDecoder.cpp#L333)

### 输出布局与显示尺寸

| `cbcrinterleave` | `nv21` | 输出布局 |
|---:|---:|---|
| 0 | 任意 | I420 |
| 1 | 0 | NV12 |
| 1 | 1 | NV21 |

map 返回硬件 surface 的 pitch 和 coded height。1080 高输入可能返回 1088 行；显示区域来自
decoder 的 `pOnStreamChanged` 回调，不是 parser sequence callback。紧凑输出应分别拷贝有效的
Y 行和 UV 行，UV 源偏移仍按 coded height 计算。

- 参考代码：[记录 display area](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/codec/IxDecoder.cpp#L26) · [裁拷 Y/UV](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/codec/IxDecoder.cpp#L274)

### 硬件缩放宽度按 64 对齐

`scaleDownWidth/scaleDownHeight` 是目标尺寸。从 1920x1080 请求 `848x480`、`854x480`
或 `896x480`，实测 map 均返回 `896x480`。下游使用实际 map 尺寸，或预先按
`(width + 63) / 64 * 64` 对齐目标宽度。

- 参考代码：[设置缩放尺寸](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/codec/IxDecoder.cpp#L85) · [转码入口对齐](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/test/test_transcode.cpp#L58)

### Context、EOS 与重连

- 每个调用 decoder/map 的线程都要设置或 push 同一个 context。
- H.264/HEVC 首次启动、重连、seek 或循环播放后，都从携带参数集的关键帧开始提交；
  `avformat_find_stream_info` 可能已消耗文件开头的 packet，不能假设首个可读 packet 可直接解码。
- 文件真正结束且不再复用 decoder 时才发送 EOS 并 drain。
- 直播流临时 EOF/I/O error 重连时保留 decoder，不发送 EOS。
- 文件循环播放复用 decoder 时直接 `seek(0)`，不发送 EOS；重新等待关键帧后继续提交。

- 参考代码：[首次等待关键帧](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/node/FFmpegReadNode.cpp#L43-L47) · [直播重连与循环播放的 EOS 分支](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/node/FFmpegReadNode.cpp#L126-L160) · [只重建 demuxer](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/node/FFmpegReadNode.cpp#L283-L294) · [解除阻塞后 join](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/codec/IxDecoder.cpp#L146-L184)

## 解码能力

| 能力 | 当前结论 |
|---|---|
| `ixvidGetDecoderCaps` / `cuvidGetDecoderCaps` | 已声明、已导出、可调用，但 caps 结果不能代替码流实测 |
| H.264/HEVC/VP9 parser | 支持，sequence/decode/display callback 均会触发 |
| H.264 4:2:0 | 支持；Baseline 实测至 8192x8192，Main/High 实测至 4096x2304 |
| HEVC 4:2:0 | 支持；Main 实测至 8192x8192，Main10 实测至 4096x2160 |
| H.264/HEVC 4:2:2 | 不支持；caps 返回支持是误报 |
| H.264/HEVC 4:4:4 | 不支持 |
| VP9 4:2:0 8-bit | 支持，直喂和 parser 均实测 256x128 |
| MPEG-4 Part 2 | 不支持，`ixvidGetDecoderCaps` 返回 `bIsSupported=0` |
| AVS2 | 不支持；caps 虽返回支持，但没有正向 bitstream 证据 |
| AV1 | 不支持，caps 返回不支持 |
| I420/NV12/NV21 map | 支持，布局关系已逐字节验证 |
| `ixvidRefreshDecoder` | 头文件声明但库未导出，不支持 |

`nOutputFormatMask` 对已支持组合也可能返回 0。完整证据见
[decoder capability probe](../scripts/corex-decoder-capability/README.md)。

## 验证用例

| 用例 | 内容 | 执行 |
|---|---|---|
| [corex-decoder-capability](../scripts/corex-decoder-capability/) | caps、parser、直喂、布局、profile、尺寸、异步 map | `bash build.sh && bash run.sh` |

`run.sh` 不接收参数，一次性执行该用例的全部验证。素材由
[`assets/videos/generate.sh`](../assets/videos/generate.sh) 按需生成。

## 参考实现

### [trt_yolo_video_pipeline：硬件解码数据流](https://github.com/121786404/trt_yolo_video_pipeline/tree/iluvatar)

| 类 / 文件 | 职责 |
|---|---|
| [`IxDecoder`](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/codec/IxDecoder.cpp) | context、decoder/parser、独立 map 线程、PTS、EOS、裁剪和缩放 |
| [`FFmpegReadNode`](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/node/FFmpegReadNode.cpp) | demux 后处理、Annex-B BSF、文件 drain、直播流重连与关键帧恢复 |

[iluvatarpipeline](https://gitee.com/121786404/iluvatarpipeline) 提供直喂 + 独立取帧线程参考：
[IluvatarVideoDecoder.cpp](https://gitee.com/121786404/iluvatarpipeline/blob/master/src/modules/codec/IluvatarVideoDecoder.cpp#L177) ·
[FFmpegDemuxer.cpp](https://gitee.com/121786404/iluvatarpipeline/blob/master/src/modules/codec/FFmpegDemuxer.cpp#L917)。
