# 设备与作用域选项（Devices & Scope）

> 本文档概述 `ixsys` 的设备与作用域选项。权威来源以 `ixsys -h` 与官方手册为准。

## 选项说明

| 选项 | 说明 |
|------|------|
| `--devices TEXT` | 约束 `--trace` 中相关项（`cuda/mem/power/temperature/osrt/nvtx/cudnn/cuinfer/video/raw_syscalls/proc_kthread`）的设备作用域。可取 `all`（默认）或逗号分隔的物理设备 ID。 |
| `--cuda-trace-scope {process-tree,system-wide}` | CUDA 采集范围：`process-tree`（默认，目标进程及其子进程）或 `system-wide`（采集“采集开始后由同一用户启动”的 CUDA 进程）。 |
| `--target-processes-filter TEXT` | 逗号分隔的进程名表达式（按正则匹配）过滤采集对象。 |
| `--profile-from-start {on,off}` | 是否从程序启动即采集 CUDA 数据，默认 `on`。`off` 时由应用内 profiler start/stop 控制采集开关。 |

## 关键约束（与 `SKILL.md` 步骤 3 一致）

1. `--profile-from-start off` 时，必须 trace `cuda` 或其子集（`cuda*`）。
2. `--cuda-trace-scope system-wide` 必须配合 `-o`。
3. `system-wide` 且无目标应用时，命令句式固定为：`ixsys -o <file> -t cuda --cuda-trace-scope system-wide [-d <seconds>] [-l <seconds>]`。
4. `system-wide` 且无目标应用时，除 `-o` 与可选 `-d/-l` 外，不得加入其他选项（如 `--devices`、`--callstack`、`-f` 等）。

## 使用规则

1. `--devices` 与 `--gpu-metrics-device` 相互独立，可同时使用，分别限制各自作用域。
2. `--devices` 限制的是 `-o` 保存内容，不控制 `--print-gpu-*` 的终端打印范围。
3. `--target-processes-filter` 直接写进程名/表达式（逗号分隔），不要加引号或括号，避免被当作字面进程名。
4. 使用 `CTRL+C` 停止采集时，不要连续发送第二次 `CTRL+C`，否则可能直接终止 `ixsys` 并导致无数据保存。

## 快速示例

```bash
# 采集 GPU 0/1/2 的默认 trace（cuda,nvtx）
ixsys -o trace --devices 0,1,2 ./vectorAdd

# --devices 与 --gpu-metrics-device 并用（作用域独立）
ixsys -o trace --devices 0,1,2 --gpu-metrics-device 3 ./vectorAdd

# 带目标应用的 system-wide
ixsys -o trace --trace cuda,osrt --cuda-trace-scope system-wide ./app

# 无目标应用的 system-wide（固定句式）
ixsys -o trace --trace cuda --cuda-trace-scope system-wide -d 10 -l 10
```
