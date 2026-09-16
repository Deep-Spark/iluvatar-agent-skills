# Corex encoder capability probe

验证当前 `libnvencode` 编码格式、profile 和分辨率能力，不依赖解码器。程序直接使用
`<IX/ixcodec/ixEncodeAPI.h>` 和 `IxEnc*` 原生 API。

## 结果

PASS。所有 15 个格式/profile/尺寸 case 均生成非空码流，通过 `ffprobe`，并可由 FFmpeg 解码首帧。

## 验证内容

- H.264 4:2:0 8-bit。
- HEVC Main 4:2:0 8-bit 和 Main10 4:2:0 10-bit。
- HEVC NV16/P210 4:2:2 输入 layout；输出码流仍分别是 `yuv420p`、`yuv420p10le`。
- H.264 `profile=0/1/2/3` 分别输出 High/Baseline/Main/Extended。
- `256x128`、`264x128`、`256x136` 和 `8192x8192` 均可编码。
- 每个码流经 `ffprobe` 检查 codec/profile/pixel format，并解码首帧确认输出有效。

编码 timing 字段由 probe 显式设置：H.264 使用 `numUnitsInTick=1,timeScale=60`，HEVC 使用
`numUnitsInTick=1,timeScale=30,numTicksPocDiffOne=1`，避免生成无效的 VUI timing。

## 关键输出

| 输入配置 | 输出码流 |
|---|---|
| H.264 profile 0/1/2/3 | High/Baseline/Main/Extended，`yuv420p` |
| HEVC 420 8-bit | Main，`yuv420p` |
| HEVC 420 10-bit | Main 10，`yuv420p10le` |
| HEVC NV16 4:2:2 输入 | Main，`yuv420p` |
| HEVC P210 4:2:2 10-bit 输入 | Main 10，`yuv420p10le` |
| H.264/HEVC 8192x8192 | 非空码流且可解码 |

## 运行

```bash
bash build.sh
bash run.sh
```

`run.sh` 无参数，一次性执行全部 case。raw 素材和编码输出位于
`../../assets/videos/` 及其 `outputs/encoder/` 子目录，均由 `.gitignore` 忽略。

本用例验证的是列出的 source layout、profile 和尺寸，不证明编码输出保留 4:2:2 chroma。
