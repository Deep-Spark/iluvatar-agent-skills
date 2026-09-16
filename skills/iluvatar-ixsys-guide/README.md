# iluvatar-ixsys-guide

`iluvatar-ixsys-guide` 是面向 `ixsys` 的 Agent Skill：把用户自然语言需求转换为可复制命令，并解释关键选项的选择依据与约束。

当前语义基线：`ixToolkit v5.0.0`。

## 能力范围

- 生成标准命令骨架：`ixsys [OPTIONS] [cuda-app] [app-args]`
- 生成查询类命令：`--help`、`--version`、`--sessions-list`、`--gpu-metrics-device help`、`--gpu-metrics-set help`
- 生成仅终端打印命令：`--print-api/gpu-*`（遵守白名单约束）
- 生成无目标应用的 system-wide 采集命令：`--cuda-trace-scope system-wide`（固定句式）
- 生成常规采集命令：`-o/--output`、`--trace`、`--devices`、`--cuda-trace-scope`、`--print-*` 等

## 一键安装（推荐）

见 [从仓库本地安装](../../README.md#从仓库本地安装)。

## 工具对照

| 工具 | 用户级安装位置 | 显式触发 |
|---|---|---|
| Cursor / Codex CLI / Copilot | `~/.agents/skills/iluvatar-ixsys-guide/` | `/iluvatar-ixsys-guide <问题>`（Cursor）/ `$iluvatar-ixsys-guide <问题>`（Codex CLI）/ 对应技能触发语法（Copilot） |
| Claude Code | `~/.claude/skills/iluvatar-ixsys-guide/` | `/iluvatar-ixsys-guide <问题>` |

## 如何触发

1. 显式触发（推荐）：`/iluvatar-ixsys-guide <你的问题>`
2. 隐式触发：直接自然语言提问，由模型根据 `SKILL.md` 的描述自动匹配

## 使用示例

- “给 `./vectorAdd` 生成 `ixsys` 命令，保存 trace 并打印 API summary。”
- “我只想看终端 GPU trace，不保存数据。”
- “给我一个 system-wide（无 app）的命令。”
- “列出可用的 GPU metrics 设备和 metric set。”

## 核心约束（验收重点）

- 命令路径分流：查询类、仅终端打印、无目标应用 system-wide、常规采集四类明确分流。
- 查询类 / 仅终端打印（无 `-o`）/ 无目标应用 system-wide：都在步骤 1 结束，不进入步骤 2-4。
- 常规采集：默认要求 `-o/--output`，且要求目标应用 `[cuda-app] [app-args]`。
- 仅终端打印（无 `-o`）仅允许额外出现：
  - `--profile-from-start`
  - `--target-processes-filter`
  - `--demangling`
  - `--print-kernel-args`
  - `--deferred_trace`
  - `--limited_trace`
- 无目标应用 system-wide 固定句式：`ixsys -o <file> --trace cuda --cuda-trace-scope system-wide [-d <seconds>] [-l <seconds>]`，除固定选项及可选 `-d/-l` 外不得增加其他选项。

## 文档映射（步骤 1-4）

- 步骤 1（采集路径判断）：`references/cli-query.md`、`references/cli-display.md`
- 步骤 2（追踪项与采集指标）：`references/cli-trace.md`、`references/cli-metricsProfiling.md`
- 步骤 3（时间/过滤/设备/作用域）：`references/cli-timeAndFilter.md`、`references/cli-deviceAndScope.md`
- 步骤 4（展示与输出）：`references/cli-display.md`、`references/cli-output.md`

## 单一事实来源（SoT）

- 规则语义与行为约束：以 `SKILL.md` 为准（机器执行规范）
- 参数与组合约束：以 `ixsys -h` 与 `references/*.md` 一致性为准
- 安装与使用说明：以本 `README.md` 为准（面向使用者）

## 维护建议

- 修改 `SKILL.md` 后同步更新 `references/`
- 调整规则时先核对 `ixsys -h`，再同步到 `SKILL.md` 与 `references/`
- 本目录无编译用例与测试脚本
