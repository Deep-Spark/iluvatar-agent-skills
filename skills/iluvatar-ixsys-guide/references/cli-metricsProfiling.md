# 指标采集类选项（Metrics & Profiling）

> 本文档概述 `ixsys` 的指标采集类选项。权威来源以 `ixsys -h` 与官方手册为准。

## 选项说明

| 选项 | 说明 |
|------|------|
| `-c, --callstack` | 开启 C/C++ callstack 采样（需 sudo/root）。默认：`--kernel false`、`--frequency 1000`。 |
| `--kernel <true|false>` | callstack 子参数：是否采集内核态 callstack。默认 `false`。 |
| `--frequency <1-10000>` | callstack 子参数：采样频率（Hz）。默认 `1000`。 |
| `--gpu-metrics-device` | GPU metrics 总开关。参数为 `none` 或 `--gpu-metrics-device help` 输出的 GPU ID。默认 `none`。 |
| `--gpu-metrics-set` | GPU metrics 集合索引，取值来自 `--gpu-metrics-set help`。未设置时默认选中该 GPU 支持的第一个 set。 |
| `--gpu-metrics-frequency <10-200000>` | GPU metrics 采样频率（Hz），默认 `500`。 |
| `--nic-metrics {true,false}` | 是否采集 NIC/HCA 指标，默认 `false`。 |
| `--python-sampling {true,false}` | 是否开启 Python 回溯采样（需 sudo/root），默认 `false`。 |
| `--python-sampling-frequency <1-1000>` | Python 回溯采样频率（Hz），默认 `1000`。 |
| `--event-sync {true,false}` | 采集跨流同步信息（`cudaEventRecord`/`cudaStreamWaitEvent`），默认 `false`。 |

## 关键约束（与 `SKILL.md` 步骤 2 一致）

1. `--kernel`、`--frequency` 必须与 `-c, --callstack` 同用。
2. `--callstack` 与 `--python-sampling` 需要 root/sudo 权限。
3. 仅当 `--gpu-metrics-device` 设置为有效设备（非 `none`、非 `help`）时，`--gpu-metrics-set` 与 `--gpu-metrics-frequency` 才有意义。
4. `--event-sync true` 时，必须同时 trace GPU activities（`--trace cuda_device` 或 `--trace cuda`）。

## 使用规则

1. `--gpu-metrics-device help`、`--gpu-metrics-set help` 用于能力发现，不会开启采集。
2. `--gpu-metrics-set` 除预定义集合外，还支持环境变量 `IXSYS_USER_DEFINED_METRIC_SET` 定义的自定义集合。
3. `--gpu-metrics-device` 不支持多架构 GPU 混合采集（将报错退出）。

## 快速示例

```bash
# 采集 callstack（默认 trace 为 cuda,nvtx）
sudo ixsys -o trace -c ./vectorAdd

# 采集 GPU 0 的 metrics
ixsys -o trace --gpu-metrics-device 0 ./vectorAdd

# Python 回溯采样（500Hz）
sudo ixsys -o trace --python-sampling true --python-sampling-frequency 500 python3 test.py

# 仅 trace cuda_device，并采集同步信息
ixsys -o trace --trace cuda_device --event-sync true ./app
```
