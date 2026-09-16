# ixjpeg-decode-check

## 目的

遍历 `../../assets/images/` 中的 JPEG 测试图，验证 Corex IxJPEG 单图 `nvjpegDecode` 的入口治理：

- 不支持的 JPEG 规格必须在进入 IxJPEG 前 preflight 拦截。
- 被本 case 接受的图片会进入 IxJPEG `NVJPEG_OUTPUT_RGB` 解码流程，且必须返回 SUCCESS 并真实写出数据。



## 结果

PASS —— preflight 在进入 IxJPEG 前正确拦截当前资产中的 5 类不支持规格（progressive SOF2 / precision≠8 / lossless SOF3 / arithmetic SOF9 / JPEG2000），其余被接受的图片 IxJPEG `NVJPEG_OUTPUT_RGB` 解码均 SUCCESS 且写出数据。另执行 26 项能力检查：9 种输出格式写入、NV12/NV21 与 YUV 的逐字节排布对照、420/422/444 YUV 与 libjpeg raw YCbCr 对照、batched state 切换格式与重建 state workaround、EOI 尾随数据、8 种 EXIF Orientation，以及 DHT/DQT marker 边界。

## Preflight 方法

图片信息读取分两层：

- libjpeg (`jpeg_mem_src` + `jpeg_read_header`) 读取标准 JPEG header，用于判定宽高、component、precision、colorspace、progressive/arithmetic。
- 轻量 marker parser 补充 libjpeg 不直接暴露的 SOF marker、SOS/DHT/DQT 数量、component/sampling factor、marker 截断/未知 marker、SOF 前 EOI、SOS 后缺 EOI，以及 JP2 signature 分流。SOF1 仍按 marker `0xFFC1` 拦截，但当前资产不再保留 SOF1 测试图。

## 解码流程

被本 case 接受的图片仍会进入 IxJPEG 解码流程。每个输出 plane 先用 `0xA5` sentinel 初始化，`nvjpegDecode` 后拷回 host，统计 changed bytes 和 checksum：

- `SUCCESS` 且 `changed > 0`：通过。
- 任何 decode 失败或 `changed == 0`：本 case 失败。

## 编译运行

```bash
bash build.sh
```

`build.sh` 会编译 `ixjpeg-decode-check.out`，然后直接遍历 `../../assets/images/`。

也可以用 scan 模式批量输出 TSV 报告：

```bash
./build/ixjpeg-decode-check.out --scan ../../assets/images ixjpeg-decode-check-report.tsv
```

## 输出

```text
SKIP progressive/progressive_420.jpg before_ixjpeg=true reason="progressive JPEG SOF2 is unsupported" ...
SKIP precision_12.jpg before_ixjpeg=true reason="sample precision is not 8-bit" ...
SKIP lossless_sof3.jpg before_ixjpeg=true reason="lossless/non-DCT JPEG SOF3 is unsupported" ...
SKIP arithmetic_sof9.jpg before_ixjpeg=true reason="arithmetic-coded sequential JPEG SOF9 is unsupported" ...
SKIP jpeg2000.jp2 before_ixjpeg=true reason="JPEG2000 JP2 signature is not baseline DCT JPEG" ...
PASS baseline_420_even.jpg fmt=y status=SUCCESS changed=3050 ...
PASS baseline_420_even.jpg yuv_ref css=420 ... y_max=1 u_max=1 v_max=1 ...
PASS baseline_422_even.jpg yuv_ref css=422 ... y_max=1 u_max=1 v_max=1 ...
PASS baseline_444_even.jpg yuv_ref css=444 ... y_max=1 u_max=1 v_max=1 ...
PASS baseline_420_even.jpg fmt=nv12_exact status=SUCCESS y_match=1 chroma_match=1
PASS baseline_420_even.jpg fmt=nv21_exact status=SUCCESS y_match=1 chroma_match=1
PASS batched_state_format_switch same_state_kept_first_format=1 recreate_state_applied_new_format=1
SUMMARY PASS ixjpeg-decode-check inputs=35 decoded=30 skipped=5 feature_checks=26
```
