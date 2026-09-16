# 追踪项选项（Trace Options）

> 本文档概述 `ixsys` 的追踪项选项。权威来源以 `ixsys -h` 与官方手册为准。

## 选项说明

| 选项 | 说明 |
|------|------|
| `-t, --trace <tracer>[,...]` | 选择 trace 类型。支持：`cuda`、`osrt`、`nvtx`、`cudnn`、`cuinfer`、`video`、`raw_syscalls`、`mem`、`power`、`temperature`、`cuda_api`、`cuda_device`、`cuda_memcpy`、`cuda_memset`、`cuda_kernel`、`cuda_memcpy_p2p`、`proc_kthread`。 |
| `--nccl-trace TEXT` | 选择 NCCL trace 子类型。支持：`api`、`api-group`、`api-coll`、`api-p2p`、`rt`、`coll`、`p2p`、`kernel-launch`、`proxy-op`、`proxy-step`、`all`。 |

## 语义速记

1. `cuda` 包含 `cuda_api` + `cuda_device`；`cuda_device` 包含 `cuda_memcpy` + `cuda_memset` + `cuda_kernel` + `cuda_memcpy_p2p`。
2. `osrt`、`raw_syscalls`、`proc_kthread` 对应 CPU 侧运行时/系统调用/线程调度信息。
3. `mem` 对应 GPU 内存使用情况；`power`、`temperature` 对应功耗与温度。
4. `cudnn`、`cuinfer`、`video` 分别对应相关库调用追踪。
5. 未手动指定 `--trace` 时，命令生成默认显式写 `--trace cuda,nvtx`。
6. `--cuda-trace-scope system-wide` 且无目标应用时，`--trace` 必须为 `cuda`（详见 `./cli-deviceAndScope.md`）。

## 关键约束（与 `SKILL.md` 步骤 2 一致）

1. `--trace` 与 `--nccl-trace` 多值都只允许逗号分隔，且逗号两侧不能有空格。
2. 依赖约束：`raw_syscalls`、`proc_kthread` 必须同写 `osrt`。
3. 依赖约束：`power`、`temperature` 必须同写至少一个 CUDA 族项（`cuda|cuda_device|cuda_memcpy|cuda_memset|cuda_kernel|cuda_memcpy_p2p`）。
4. 父子同写建议归并为父项：如 `cuda,cuda_api -> cuda`、`cuda_device,cuda_kernel -> cuda_device`。
5. NCCL 父子同写建议归并为父项：如 `api-group,api-coll,api-p2p -> api`；`api,rt,proxy-op,proxy-step -> all`。

## 快速示例

```bash
# 默认 trace（cuda,nvtx）
ixsys -o trace ./vectorAdd

# 采集线程调度与 CUDA 相关信息
ixsys -o trace --trace cuda,osrt,proc_kthread ./vectorAdd

# 采集 GPU 功耗（需同时带 CUDA 族 trace）
ixsys -o trace --trace cuda,power ./app

# 采集 NCCL API（trace 仍走默认 cuda,nvtx）
ixsys -o trace --nccl-trace api ./nccl_test
```
