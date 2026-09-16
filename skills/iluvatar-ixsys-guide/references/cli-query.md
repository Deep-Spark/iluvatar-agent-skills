# 查询类选项（Query Options）

> 本文档概述 `ixsys` 的查询类选项。权威来源以 `ixsys -h` 与官方手册为准。

## 选项说明

| 选项 | 说明 |
|------|------|
| `-h, --help` | 打印 `ixsys` 帮助信息。代码要求 **`argc == 2`** |
| `-v, --version` | 打印 `ixsys` 版本信息。代码要求 **`argc == 2`** |
| `--sessions-list` | 查询当前正在运行的 `ixsys` 会话。代码要求 **`argc == 2`** |
| `--gpu-metrics-device help` | 查询 GPU metrics 可用设备信息。代码要求 **`argc == 3`** |
| `--gpu-metrics-set help` | 查询 GPU metrics 可用集合信息。代码要求 **`argc == 3`** |

## `--sessions-list` 字段说明

| 字段 | 含义 |
|------|------|
| `ID` | 被 `ixsys` 采集的目标进程 PID |
| `TIME` | 目标进程运行时间 |
| `STATE` | 采集状态：`ConfiguredLaunched`、`DelayedCollection`、`Collection`、`FinishedCollection` |
| `NAME` | 目标进程名 |

## 使用规则

1. 查询类命令为固定命令，参数不可增删。
2. 查询类需求在 `SKILL.md` 的步骤 1 直接结束，不进入采集步骤 2-4。
3. 查询类命令建议单独执行，不与 `-o`、`-t`、`[cuda-app] [app-args]` 组合。
4. `--gpu-metrics-device help` 与 `--gpu-metrics-set help` 仅用于能力发现，不会开启 metrics 采集（详见 `./cli-metricsProfiling.md`）。
5. 文档示例优先使用长选项（`--help`、`--version`）；短选项 `-h`、`-v` 与之等价。

## 快速示例

```bash
ixsys --help
ixsys --version
ixsys --sessions-list
ixsys --gpu-metrics-device help
ixsys --gpu-metrics-set help
```
