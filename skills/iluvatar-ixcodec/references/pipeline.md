# trt_yolo_video_pipeline 完整参考实现

[trt_yolo_video_pipeline](https://github.com/121786404/trt_yolo_video_pipeline/tree/iluvatar)
不只是转码示例，而是用 graph runtime 组合视频输入、硬件编解码、推理、跟踪、GPU OSD、
录制和网络输出的视频分析框架。

[`Pipeline`](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/include/graph/runtime/Pipeline.h)
和 [`ProcessNode`](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/include/graph/runtime/ProcessNode.h)
负责节点生命周期与上下游连接；[`ThreadSaveQueue`](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/include/graph/runtime/ThreadSaveQueue.h)
在解码、推理、OSD、编码速度不一致时传递数据。
项目使用统一的 `Decoder` / `Encoder` 接口组织 NVIDIA 与 Corex 实现，上层节点不直接依赖
`Nv*` 或 `Ix*` 类型。

`HardwareDecoder` / `HardwareEncoder` 根据 `__ILUVATAR__` 选择 `IxDecoder` / `IxEncoder`
或 `NvDecoder` / `NvEncoder`。源码中可组合出三类典型数据流：

- 转码：FFmpeg demux/bitstream filter → 硬件解码 → GPU NV12 → 可选硬件缩放 → 硬件编码 → mux。
- 检测与输出：硬件解码 → 引擎推理 → GPU OSD → 硬件编码 → 推流或录像，也可保存 JPEG 快照。
- 跟踪与结构化分析：检测或 FairMOT → ROI 特征/分类/车牌识别 → JDE、DeepSORT 或 OC-SORT →
  GPU OSD → 编码输出。

## 功能模块

| 能力 | 源码入口 | 实际用途 |
|---|---|---|
| 平台编解码抽象 | [`HardwareDecoder.h`](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/codec/HardwareDecoder.h) / [`HardwareEncoder.h`](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/codec/HardwareEncoder.h) | 编译期选择 Corex 或 NVIDIA 实现，保持上层接口一致 |
| 视频输入与解码 | [`FFmpegReadNode`](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/node/FFmpegReadNode.cpp) / [`IxDecoder`](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/codec/IxDecoder.cpp) | 文件或网络流 demux、Annex-B BSF、parser、独立 map 线程、PTS/EOS 和重连 |
| 本地引擎推理 | [`YoloInfer`](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/infer/trt/YoloInfer.cpp) / [`MultipleInferenceInstances`](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/include/graph/infer/core/MultipleInferenceInstances.h) | GPU 图像预处理、批量推理和检测结果解析；按 `device_list` 创建实例并轮询分发任务 |
| GPU OSD | [`ImageDrawNode`](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/node/ImageDrawNode.cpp) | 在 NV12/BGR 图像上绘制框、旋转框、标签、track ID、属性、计数、mask 和关键点 |
| 多目标跟踪 | [`TrackNode`](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/node/TrackNode.cpp) | 组合 JDE/ByteTrack 路径、DeepSORT 和 OC-SORT；跟踪结果可直接交给 OSD |
| 结构化分析 | [`FairMotInfer`](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/include/graph/infer/trt/FairMotInfer.h) / [`RoiClassifierInfer`](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/include/graph/infer/trt/RoiClassifierInfer.h) / [`RoiEmbeddingInfer`](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/include/graph/infer/trt/RoiEmbeddingInfer.h) / [`VehiclePlateInfer`](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/include/graph/infer/trt/VehiclePlateInfer.h) | FairMOT、ROI 属性分类、ReID embedding、车牌检测与识别 |
| Triton 推理 | [`TritonGrpcClient`](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/include/graph/infer/triton/client/TritonGrpcClient.h) / [`TritonHttpClient`](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/include/graph/infer/triton/client/TritonHttpClient.h) / [`TritonModelInfer`](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/include/graph/infer/triton/client/TritonModelInfer.h) / [`TritonServer`](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/include/graph/infer/triton/server/TritonServer.h) | gRPC/HTTP 客户端、CUDA shared memory 以及进程内 Triton Server |
| 编码与输出 | [`FFmpegOutputNode`](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/node/FFmpegOutputNode.cpp) / [`FFmpegRecordNode`](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/node/FFmpegRecordNode.cpp) / [`JpegSaveNode`](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/src/graph/node/JpegSaveNode.cpp) | 硬件编码、MP4/FLV/RTSP 输出、录像任务和 GPU NV12 JPEG 快照 |

## 代表性程序

| 入口 | 覆盖的数据流 |
|---|---|
| [`test_transcode.cpp`](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/test/test_transcode.cpp) | 多路硬件解码、可选硬件缩放、H.264/HEVC 重编码和 MP4 输出 |
| [`test_detect_osd_enc_push_multi.cpp`](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/test/test_detect_osd_enc_push_multi.cpp) | 多路解码共享推理实例，再分别 OSD、编码和输出 |
| [`test_detect_osd_enc_record.cpp`](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/test/test_detect_osd_enc_record.cpp) / [`test_detect_osd_jpeg.cpp`](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/test/test_detect_osd_jpeg.cpp) | 检测、OSD、推流/录像以及 JPEG 快照 |
| [`test_pptracking_jde_pipeline.cpp`](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/test/test_pptracking_jde_pipeline.cpp) / [`test_pptracking_deepsort_pipeline.cpp`](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/test/test_pptracking_deepsort_pipeline.cpp) / [`test_pptracking_sde_pipeline.cpp`](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/test/test_pptracking_sde_pipeline.cpp) | JDE、DeepSORT、ByteTrack/OC-SORT 三类视频跟踪流水线 |
| [`test_image_ppvehicle.cpp`](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/test/test_image_ppvehicle.cpp) | 车辆检测、属性分类、车牌检测和识别 |
| [`test_triton_yolo_pipeline.cpp`](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/test/test_triton_yolo_pipeline.cpp) / [`test_triton_grpc_demo.cpp`](https://github.com/121786404/trt_yolo_video_pipeline/blob/iluvatar/test/test_triton_grpc_demo.cpp) | Triton YOLO 视频流水线和 CUDA shared memory gRPC 调用 |

复用该项目时先选择与目标业务最接近的代表性程序，沿 graph node 追踪数据流。涉及 Corex
编解码迁移差异时，再读取[解码参考](decoder.md)和[编码参考](encoder.md)。
