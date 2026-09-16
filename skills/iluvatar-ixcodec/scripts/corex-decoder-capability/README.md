# Corex decoder capability probe

验证当前 `libnvcuvid` 解码能力，不依赖编码器。程序直接使用
`<IX/ixcodec/ixviddec.h>` 和 `ixvid*` 原生 API。

## 结果

PASS。H.264/HEVC/VP9 parser、直喂、布局、profile、缩放和 8192x8192 正样本均通过；
H.264/HEVC 4:2:2、4:4:4 均没有输出帧，按不支持处理。

## 验证内容

- API：`ixvidGetDecoderCaps`、`ixvidCreateVideoParser`、`ixvidParseVideoData` 已导出且可调用；`ixvidRefreshDecoder` 虽在头文件声明，但库未导出。
- MPEG-4 Part 2：`ixvidGetDecoderCaps` 返回 `bIsSupported=0`，按不支持处理。
- parser：H.264/HEVC Annex-B 和 VP9 frame payload 均触发 sequence/decode/display 和 EOS decode callback，并完成 decode/map。
- 直喂：H.264/HEVC Annex-B 和 VP9 frame payload 经 `ixvidDecodePicture` 提交，后台线程用 `ixvidMapVideoFrame` 取帧。
- timestamp：`IXVIDSOURCEDATAPACKET` 和 `IXVIDPARSERDISPINFO` 没有 timestamp 字段；parser
  路径 map 返回 0。直喂写入 `IXVIDPICPARAMS.timestamp=1` 时，map 返回 1。
- profile/level：H.264 Constrained Baseline/Main/High @ L5.2，HEVC Main/Main10 @ L5.1 High-tier。
- chroma：H.264/HEVC 4:2:0 可解码；H.264/HEVC 4:2:2、4:4:4 均作为不支持负样本。
- 输出布局：将同一帧分别 map 为 I420、NV12、NV21，拷回 host 后逐字节验证三种布局的 Y/U/V 关系。
- 缩放：请求 `848x480`、`854x480`、`896x480`，实际 pitch 均为 `896`、height 均为 `480`。
- 分辨率：H.264/HEVC `8192x8192` 可 decode/map。
- 异步语义：部分 HEVC 码流在 `ixvidDecodePicture` 返回 success 后仍需 EOS 才产生显示帧，见 [decode-map-sync-output.txt](decode-map-sync-output.txt)。

`ixvidGetDecoderCaps` 只能作提示，不能代替 bitstream 实测。当前 caps 对 H.264/HEVC 4:2:2 返回 `bIsSupported=1`，但实际码流没有输出帧；`nOutputFormatMask` 对已支持组合也返回 `0`。
VP9/AVS2 的 caps 都返回 `bIsSupported=1`；VP9 已有直喂和 parser 正样本，AVS2 没有正向 bitstream 证据，因此不列为支持能力。AV1 caps 返回不支持。

## 关键输出

| 检查 | 结果 |
|---|---|
| timestamp 字段 | `source_packet=0 display_info=0 picture_params=1` |
| MPEG-4 Part 2 | caps 返回 `bIsSupported=0`，UNSUPPORTED |
| H.264 parser | `picture_decode=1 eos_decode=1 map_timestamp=0` |
| HEVC parser | `picture_decode=1 eos_decode=1 map_timestamp=0` |
| VP9 parser | `picture_decode=1 eos_decode=1 map_timestamp=0` |
| 直喂 timestamp | `map_timestamp=1` |
| I420/NV12/NV21 | `layout_content: PASS` |
| 1920x1080 请求 848/854/896x480 | 均返回 `pitch=896 height=480` |
| H.264/HEVC 8192x8192 | decode/map PASS |
| VP9 4:2:0 8-bit 256x128 | 直喂与 parser decode/map PASS |
| H.264/HEVC 4:2:2、4:4:4 | 无输出帧，UNSUPPORTED |

## 运行

```bash
bash build.sh
bash run.sh
```

`run.sh` 无参数，一次性执行全部正负样本。素材由
`../../assets/videos/generate.sh` 生成；日志、map 输出和生成码流位于
`../../assets/videos/outputs/decoder/`，均由 `.gitignore` 忽略。

4:2:2/4:4:4 负样本会使解码器进入非法状态，脚本在每个负样本后 reset GPU，避免影响后续测试。
本用例能证明列出的码流、格式和尺寸行为，不代表所有 profile、level 和码流特征组合。
