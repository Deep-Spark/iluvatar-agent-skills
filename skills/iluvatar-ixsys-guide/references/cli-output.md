# 输出选项（Output Option）

> 本文档概述 `ixsys` 的输出选项。权威来源以 `ixsys -h` 与官方手册为准。

## 选项说明

| 选项 | 说明 |
|------|------|
| `-o, --output <file>` | 将 profiling 数据写入文件。若文件同名，默认覆盖。 |

## 使用规则

1. 需要保存采集结果时，必须设置 `-o, --output <file>`。
2. `-o` 保存的是 trace 数据和 metrics profiling 数据，不保存终端打印文本（详见 `./cli-trace.md`、`./cli-metricsProfiling.md`、`./cli-display.md`）。
3. 除查询类命令（详见 `./cli-query.md`）和“仅终端打印（无 `-o`）”场景外，建议默认使用 `-o` 保留可复盘数据。
4. 输出文件可在终端用 `sqlite3`（>= 3.22.0）查看，也可在 [ixsys-ui](https://ui.ixsys.iluvatar.com/) 中可视化分析。

## 快速示例

```bash
# 保存默认 trace（cuda,nvtx）
ixsys -o trace ./vectorAdd

# 保存 trace + callstack
sudo ixsys -c -o trace ./vectorAdd
```