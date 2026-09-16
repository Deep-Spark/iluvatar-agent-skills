# Corex IxJPEG 不支持的 JPEG 格式（硬件/解码器限制）

Corex IxJPEG 硬件解码器只支持 **baseline DCT、8-bit、≤3 components** 的 JPEG。下列格式属于硬件/解码器不支持，必须在进入 IxJPEG（`nvjpegDecode`）前 preflight 拦截、不能直接喂给解码器。

复现与入口治理见 [`scripts/ixjpeg-decode-check/`](../scripts/ixjpeg-decode-check/)。

## 1. 按规格拦截（有测试图）

| 文件 | 拦截原因 | 识别依据 |
|---|---|---|
| 当前无本地测试图 | extended sequential JPEG 不支持 | SOF1 marker `0xFFC1` |
| `progressive/progressive_420.jpg` | progressive JPEG 不支持 | libjpeg 读 header 后 `progressive=1`，SOF2 |
| `precision_12.jpg` | 非 8-bit 采样精度不支持 | SOF precision=12 |
| `lossless_sof3.jpg` | lossless / 非 DCT JPEG 不支持 | SOF3 |
| `arithmetic_sof9.jpg` | arithmetic-coded sequential JPEG 不支持 | libjpeg 读 header 后 `arithmetic=1`，SOF9 |
| `jpeg2000.jp2` | JPEG2000 不是 baseline DCT JPEG | JP2 signature box |

> SOF1 / extended sequential JPEG 的 preflight 代码路径仍在，按 marker `0xFFC1` 拦截；
> `assets/images/` 当前不再保留 SOF1 测试图。
>
> multi-SOS（SOS marker 数量不等于 1）的 preflight 代码路径仍在（见下表「SOS marker 数量 > 1」），但 `assets/images/` 当前未附 multi-SOS 测试图。

## 2.  `nvjpegGetImageInfo` 返回 `NVJPEG_STATUS_BAD_JPEG` 的输入

scan/preflight 逻辑还会拦截以下会被 SDK 判 `BAD_JPEG` 的输入：

判定逻辑集中在 [`scripts/ixjpeg-decode-check/ixjpeg-decode-check.cpp`](../scripts/ixjpeg-decode-check/ixjpeg-decode-check.cpp) 的 `preflight_reason()`（约 `:167-188`）；它消费的 marker 标志在 `parse_markers()`（约 `:251-365`）里计算。

| 类型 | 拦截原因 | 对应代码（`ixjpeg-decode-check.cpp`） |
|---|---|---|
| 非 JPEG / JP2 | 不交给 IxJPEG JPEG 解码器 | `preflight_reason :169-170`（`is_jp2_signature()` / `m.has_soi` @`:254`） |
| JPEG marker 段长度不足、长度字段非法或段越界 | `truncated JPEG marker segment` | `preflight_reason :184`（`m.truncated_marker` @`:279,284,295,301`） |
| DQT/DHT 表内部长度不完整 | `truncated JPEG marker segment` | `preflight_reason :184`（`m.truncated_marker` @`:314`，DQT/DHT 段） |
| SOF 前遇到 EOI | `EOI marker before SOF header` | `preflight_reason :181`（`m.eoi_before_sof` @`:272`） |
| APP/COM/DRI/RST/SOF/SOS/DQT/DHT 以外的未识别 marker | `unknown JPEG marker before SOF` | `preflight_reason :185`（`m.unknown_marker` @`:350`） |
| SOF 解析失败或缺失 | `invalid or truncated SOF header` / `missing SOF header` | `preflight_reason :182,186`（`m.invalid_sof` @`:327,334`） |
| component 数量 > 3 | `more than 3 JPEG components is unsupported` | `preflight_reason :178`（`li.components` @`:148` / `m.components` @`:332`） |
| 411/410 或非标准 sampling factor | `unsupported JPEG sampling factor` | `preflight_reason :179`（`m.unsupported_sampling` @`:343` → `supported_sampling_factor()` `:194-206`） |
| SOS marker 数量 > 1 | `more than one SOS marker` | `preflight_reason :180`（`m.sos` 计数） |
| SOS 后缺少 EOI | `missing EOI marker after scan data` | `preflight_reason :183`（`m.sos>0 && !m.saw_eoi`，`saw_eoi` @`:271`） |
