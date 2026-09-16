# Corex encoder feature probe

验证当前 `libnvencode` 的编码功能。程序直接使用
`<IX/ixcodec/ixEncodeAPI.h>` 和 `IxEnc*` 原生 API。

## 结果

PASS。全部正样本生成可完整解码的码流，负能力检查与空输出断言均通过。

## 验证内容

- API function list：`IxEncodeAPICreateInstance` 返回完整函数表，probe 使用的创建、初始化、编码、锁流、资源注册和销毁入口均非空。
- B-frame：H.264/HEVC 的 `IBBB`、`IBPBP`、`IBBBP`、`IBBBB`、`RA_IB` 输出中存在真实 B 帧。
- multi-reference：H.264 SPS 和 HEVC SPS/PPS 可区分单参考与多参考配置。
- hierarchical temporal layer：H.264/HEVC custom GOP 输出均解析出 temporal ID `0/1/2`；标准 `RA_IB` 仍只有 temporal ID `0`。
- HEVC max-merge：`maxNumMerge=1/2` 均写入 slice header。
- HEVC strong-intrasmoothing：`strongIntraSmoothEnable=0/1` 均写入 SPS。
- HEVC lossless：解码后的 NV12 与输入逐字节一致。
- H.264 interlaced：API 无 field/MBAFF/PAFF 配置，输出 SPS 为 progressive。
- 不支持：lookahead、质量/速度 preset、H.264 lossless、alpha、AV1、YUV444 输入。
- reconfigure：入口和 function-list 指针存在，但当前头文件明确标注 `Not support yet`。
- sequence parameters：没有 SPS/PPS/VPS 获取入口，应用需要从输出码流提取。

YUV444 的判断以 `IX_ENC_SRC_FORMAT` 为准。输入缓冲分配枚举中存在
`BUFFER_FORMAT_444`，不等于编码器支持 YUV444 source format。

`bitFormat=13/14/15` 调用编码接口虽返回 success，但输出均为 0 字节，因此不作为支持能力。
所有正样本均使用 FFmpeg 完整解码，确认码流无解码错误。

## 关键输出

| 检查 | 结果 |
|---|---|
| lossless 配置字段 | `avc=0 hevc=1` |
| H.264 B GOP | I/B/P 帧型与各 preset 一致 |
| HEVC B GOP | `IBBB`、`RA_IB` 均包含 B 帧 |
| H.264 reference | 1/2/3/4 reference 配置均写入 SPS |
| HEVC reference | L0 active refs 1/2，RA DPB=5 |
| hierarchical | H.264/HEVC temporal ID 均为 `[0,1,2]` |
| HEVC max-merge | slice 中分别解析为 1/2 candidates |
| strong-intrasmoothing | SPS flag 分别为 0/1 |
| HEVC lossless | 解码 NV12 与输入 `cmp` 一致 |
| H.264 field encoding | `frame_mbs_only_flag=1`，progressive |
| lookahead/preset/alpha/AV1/YUV444 | 当前公开 API 无对应配置入口 |
| `bitFormat=13/14/15` | encode success，输出 0 字节 |

## 运行

```bash
bash build.sh
bash run.sh
```

`run.sh` 无参数，一次性执行全部 case。生成的输入、码流和解析日志位于
`../../assets/videos/outputs/encoder_features/`，由 `.gitignore` 忽略。

“无配置入口”的负能力结论限定于当前公开头文件和导出符号，不推断未公开硬件能力。
