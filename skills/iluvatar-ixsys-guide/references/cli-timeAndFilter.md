# 时间与过滤选项（Time & Filter）

> 本文档概述 `ixsys` 的时间与过滤选项。权威来源以 `ixsys -h` 与官方手册为准。

## 选项说明

| 选项 | 说明 |
|------|------|
| `-d, --deferred_trace <seconds>` | 延迟开始采集。取值范围 `[1, 2^32-1]`。例如 `-d 10` 表示进程启动后 10 秒开始采集。 |
| `-l, --limited_trace <seconds>` | 限制采集时长。取值范围 `[1, 2^32-1]`。例如 `-l 10` 表示采集开始后 10 秒停止。 |
| `-f, --filter_ftrace_syscalls <microseconds>` | 过滤持续时间低于阈值的 ftrace syscall 事件，减小 trace 文件体量。取值范围 `[0,1000000]`，默认 `1000`（微秒）。`-f 0` 表示不过滤。 |

## 关键约束（与 `SKILL.md` 步骤 3 一致）

1. 用户未指定时间窗口时，不默认添加 `-d/-l`。
2. 用户指定 `-d/-l` 时，取值必须在 `[1,2^32-1]`。
3. `-f` 仅影响 ftrace syscall 事件过滤，不改变其他 trace 项采集逻辑。

## 使用规则

1. 用户未提“控量/降体积”诉求时，可沿用默认过滤阈值（`1000us`），命令中不必显式写 `-f`。
2. 用户明确要求“不过滤”时，显式写 `-f 0`。
3. 增加 `-f` 后，建议在回答中说明阈值原因，避免分析结论歧义。

## 快速示例

```bash
# 启动后 10 秒再开始采集
ixsys -o trace -d 10 ./vectorAdd

# 从 5 秒后开始采集，采集 20 秒
ixsys -o trace -d 5 -l 20 --trace cuda,osrt ./vectorAdd

# 关闭 ftrace syscall 过滤
ixsys -o trace -f 0 ./vectorAdd
```

