---
name: iluvatar-ixjpeg
description: >-
  Corex IxJPEG（nvjpeg/Ixjpeg）硬件 JPEG 编解码迁移参考。编码侧：4 个入口
  （`nvjpegEncodeYUV` / `nvjpegEncodeImage` / `IxjpegEncodeYUVBatched` /
  `IxjpegDoEncoderPipeline`），19 种 YUV 输入组合（planar 7 种，NV12/NV21 各 6 种）。
  `IxjpegEncodeYUVBatched` 的 `image_format` 直接用 CSS 枚举值，
  与 `IxjpegDoEncoderPipeline` 的
  FrameFormat 码（420→0）相反。解码侧：`nvjpegDecode` / `nvjpegDecodeBatched`
  已验证 RGB/BGR/RGBI/BGRI/YUV/UNCHANGED/Y/NV12/NV21；当前头文件还
  声明 `NVJPEG_BACKEND_HARDWARE`，但本 skill 推荐路径仍用
  DEFAULT/GPU_HYBRID；负向 JPEG 格式由
  `ixjpeg-decode-check` 和 reference 维护；
  `nvjpegGetImageInfo` chroma 尺寸恒为 0。触发词：ixjpeg、corex nvjpeg、nvjpegEncodeYUV/Image、
  Ixjpeg 编码迁移、NV12 编 jpg、IxjpegEncoderParamsSetFrameFormat、
  nvjpegDecode、jpeg 解码、JPEG 格式不支持。
---

# IxJPEG 编码 API

IxJPEG 硬件图像编解码两套头：

- `nvjpeg.h` —— NV nvjpeg 兼容子集，当前头文件通过 `IX/mapping_nvjpeg.h` 映射到
  `ixjpeg*` 声明（`nvjpegEncodeYUV` / `nvjpegEncodeImage`）
- `IX/ixjpeg/ixjpeg.h` —— Corex 私有扩展（`Ixjpeg*`）

## `nvjpegChromaSubsampling_t` 枚举值

编解码 API 普遍使用此枚举（`nvjpeg.h`）：

| 枚举 | 值 |
|------|----|
| `NVJPEG_CSS_444` | 0 |
| `NVJPEG_CSS_422` | 1 |
| `NVJPEG_CSS_420` | 2 |
| `NVJPEG_CSS_440` | 3 |
| `NVJPEG_CSS_411` | 4 |
| `NVJPEG_CSS_410` | 5 |
| `NVJPEG_CSS_GRAY` | 6 |
| `NVJPEG_CSS_UNKNOWN` | -1 |

注意：`IxjpegEncodeYUVBatched` 的 `image_format` 字段直接用此枚举值；`IxjpegDoEncoderPipeline` 的 `image_format` 用 `FrameFormat` 码（420→0/422→1/…），两者约定相反。


## 编码器使用

| API | 平面排布 | 子采样 / 像素格式 | NV 对应 | demo |
|---|---|---|---|---|
| `nvjpegEncodeYUV` | planar / NV12 / NV21 | planar: 420/422/440/444/GRAY；NV12/NV21: 420/422/440/444 | ✅ 同名标准 API | `scripts/ixjpeg-encode-yuv/` |
| `nvjpegEncodeImage` | 平面 RGB/BGR、交错 RGBI/BGRI | `NVJPEG_CSS_444` / `NVJPEG_CSS_420`（Corex 仅此两档；NV 全部 7 档） | ✅ 同名标准 API | `scripts/ixjpeg-encode-image/` |
| `IxjpegEncodeYUVBatched` | planar / NV12 / NV21 | planar: 420/422/440/444/GRAY；NV12/NV21: 420/422/440/444 | ❌ Corex 私有扩展 | `scripts/ixjpeg-encode-yuv-batched/` |
| `IxjpegDoEncoderPipeline` | planar / NV12 / NV21 | 420 / 422 / 440 / 444（+ planar GRAY）；411/410 无 `image_format` 映射，不支持 | ❌ Corex 私有扩展 | `scripts/ixjpeg-encode-pipeline/` |

NV 另有裸 `nvjpegEncode` + `NVJPEG_INPUT_NV12`，**corex 没有**。

### 注意事项
- **多平面必须放一块连续 device 缓冲**：corex 按 `channel[0]` 基址 **+ 偏移** 定位后续平面（不是真用各 `channel[i]` 指针）。视频解码器输出的整帧本就连续，直接把帧基址当起点即可。
- **格式名 ↔ frame_format ↔ CSS 枚举对照**：

  | 格式名 | frame_format | `nvjpegChromaSubsampling_t` | 色度平面大小 | 备注 |
  |--------|-------------|--------|-------------|------|
  | I420 | 0 (planar) | `NVJPEG_CSS_420` | U/V 各 W/2 × H/2 | |
  | I422 | 0 (planar) | `NVJPEG_CSS_422` | U/V 各 W/2 × H | |
  | I440 | 0 (planar) | `NVJPEG_CSS_440` | U/V 各 W × H/2 | |
  | I444 | 0 (planar) | `NVJPEG_CSS_444` | U/V 各 W × H | |
  | GRAY | 0 (planar) | `NVJPEG_CSS_GRAY` | — | |
  | NV12 | 1 (UV 交错) | `NVJPEG_CSS_420` | UV 交错 W × H/2 | |
  | NV16 | 1 (UV 交错) | `NVJPEG_CSS_422` | UV 交错 W × H | |
  | — | 1 (UV 交错) | `NVJPEG_CSS_440` | UV 交错 2W × H/2 | |
  | NV24 | 1 (UV 交错) | `NVJPEG_CSS_444` | UV 交错 2W × H | |
  | NV21 | 2 (VU 交错) | `NVJPEG_CSS_420` | VU 交错 W × H/2 | |
  | NV61 | 2 (VU 交错) | `NVJPEG_CSS_422` | VU 交错 W × H | |
  | — | 2 (VU 交错) | `NVJPEG_CSS_440` | VU 交错 2W × H/2 | |
  | NV42 | 2 (VU 交错) | `NVJPEG_CSS_444` | VU 交错 2W × H | |

  `NVJPEG_CSS_410V`（NV 独有，Corex 无此枚举）不在支持范围内。

## 解码器使用

| API | 输出格式 | NV 对应 | demo |
|---|---|---|---|
| `nvjpegDecode` | Corex 已验证 RGB/BGR/RGBI/BGRI/YUV/UNCHANGED/Y/NV12/NV21；Corex 无 YUY2/UNCHANGEDI_U16 枚举。NV 对照保留 NV12/YUY2/UNCHANGEDI_U16 | ✅ 同名标准 API | `scripts/ixjpeg-decode-image/`；解码检查见 `scripts/ixjpeg-decode-check/` |
| `nvjpegDecodeBatched` | 同上，批量 | ✅ 同名标准 API | `scripts/ixjpeg-decode-image-batched/` |

Corex 输出格式实测（`nvjpegDecode` / `nvjpegDecodeBatched` 行为一致）：

| 格式 | Corex | 说明 |
|------|-------|------|
| RGB / BGR / RGBI / BGRI | ✅ | 正常 |
| YUV / UNCHANGED | ✅ | Y+chroma 三通道均写入 |
| Y | ✅ | 正常写入 |
| GRAY 输入 → RGB 输出 | ✅ | 正常写入 |
| SOF1 输入 | preflight 拦截逻辑保留 | extended sequential JPEG 按 marker `0xFFC1` 在调用 IxJPEG 前拦截；当前资产不再保留 SOF1 测试图 |
| 422/444 输入 → YUV 输出 | ✅ | 与 libjpeg raw YCbCr 参考一致（最大误差 1） |
| 多 SOS 输入 | preflight 拦截 | 2 个 SOS marker 的 JPEG 在进入 IxJPEG 前由 `scripts/ixjpeg-decode-check/` 拦截 |
| NV12 / NV21 输出 | ✅ | Y 与 YUV 输出逐字节一致；色度分别与 U/V、V/U 交错顺序逐字节一致 |
| YUY2 / UNCHANGEDI_U16 | Corex 编译排除 | Corex 当前 `nvjpeg.h` 无这两个枚举；NV 对照保留 |

### 注意事项
- **EXIF Orientation 不会自动应用**：`nvjpegDecode` 只按 SOF 原始宽高输出像素，不根据 `Exif.Image.Orientation` 做旋转/翻转。业务需要转正时，必须在 IxJPEG 解码后自行读取 EXIF 并处理；这不是 IxJPEG 的正确性失败，按潜规则记录。
- `scripts/ixjpeg-decode-check/` 覆盖 decode 入口治理和输出写入检查；负向资产、
  preflight 规则和详细验证项见该 case README 及
  [`references/unsupported-jpeg-formats.md`](references/unsupported-jpeg-formats.md)。
- 使用 `NVJPEG_BACKEND_DEFAULT`。
- Corex 的同一 `nvjpegJpegState_t` 不能通过再次调用 `nvjpegDecodeBatchedInitialize` 切换输出格式：调用返回 SUCCESS，但仍沿用第一次初始化的格式。输出格式变化时先销毁并重建 state，再调用 initialize；`scripts/ixjpeg-decode-check/` 覆盖该行为和 workaround。
- **`nvjpegGetImageInfo` chroma 尺寸 Corex 恒为 0**：`ws[1..3]/hs[1..3]` 全返回 0，只有 `ws[0]/hs[0]` 有效。分配 YUV/UNCHANGED 的 chroma 缓冲须从 `nvjpegChromaSubsampling_t` 手动推算（如 `NVJPEG_CSS_420` → U/V 各 W/2 × H/2；枚举值见上方表格）。

## 不支持或未声明 API
- 不支持 transcoding：不支持在"解码 → 变换 → 重编码"时继承源图量化表、Huffman 表或元数据。
- 不支持 Decoupled Decoding。
- 未声明：`nvjpegJpegStreamParseHeader` / `nvjpegDecodeBatchedSupported`。
- 不支持：`nvjpegGetCudartProperty`（头文件有声明、库里有符号；导出不等于支持）。


## 验证方法

纯色彩条**测不出子采样/平面排布的细错**（色度均匀），验证编码正确性须用**带细节的真实图**肉眼看，或用独立解码器（如 Python PIL）解回逐像素比对。判定通道错位/色度误读则纯色块即可（红变蓝/草地变紫一眼可见）。每个 demo 目录 `./build.sh`（corex）/ `./build_nv.sh`（原生 NV 对照）。
