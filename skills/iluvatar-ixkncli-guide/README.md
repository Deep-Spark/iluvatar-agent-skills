# iluvatar-ixkncli-guide

`iluvatar-ixkncli-guide` 是一个面向 `ixkn-cli` 的 Agent Skill：把用户自然语言需求转换为可复制命令，并明确解释为什么这么选 option。

覆盖重点：

- CUDA kernel profile 命令生成（`ixkn-cli [OPTIONS] [cuda-app] [app-args]`）
- `--set/--section`、`-k/--kernel-id`、`-s/-c`、`--devices`、`--filter-mode` 组合
- `.ixkn-rep` 导入（`-i`）与导出（`-o`）路径
- 输出形态冲突处理（`--page details` / `--csv` / `--page source` / `-o`）

当前基线：`ixToolkit v5.0.0`。

## 一键安装（推荐）

见 [从仓库本地安装](../../README.md#从仓库本地安装)。

## 工具对照表

| 工具 | 用户级安装位置 | 显式触发 |
|---|---|---|
| Cursor / Codex CLI / Copilot | `~/.agents/skills/iluvatar-ixkncli-guide/` | `/iluvatar-ixkncli-guide <问题>`（Cursor）/ `$iluvatar-ixkncli-guide <问题>`（Codex CLI）/ 对应技能触发语法（Copilot） |
| Claude Code | `~/.claude/skills/iluvatar-ixkncli-guide/` | `/iluvatar-ixkncli-guide <问题>` |

## 如何触发

两种方式：

1. 显式触发（推荐）：`/iluvatar-ixkncli-guide <你的问题>`
2. 隐式触发：直接自然语言提问，模型通过 `SKILL.md` 的描述自动匹配

建议优先显式触发，命中更稳定。

## 使用示例

- “帮我给 `./vectorAdd` 生成命令，只看 `Instruction,LaunchStats`，并导出 report。”
- “我有 `vecAdd.ixkn-rep`，请回看源码，格式要 `cuda,assembly`。”
- “只看名字里有 gemm 的 kernel，跳过前 2 次，再采 1 次。”

## 关键约束（验收重点）

- 默认显式输出：`--page details`
- 未指定指标范围：显式 `--set default`
- live 场景默认加 `--profile-child-processes`（有例外）
- `-i` 下禁止叠加 live profile option（仅步骤 5 例外）
- 命令生成层坚持“一条命令一种输出形态”

## 单一事实来源（SoT）

- 规则语义与行为约束：以 `SKILL.md` 为准（机器执行规范）
- 使用说明与安装手册：以本 `README.md` 为准（面向使用者）

## 测试说明（面向 skill 开发者）

当前目录仅保留 skill 本体（`SKILL.md` + `references/`），无编译用例与测试脚本。

## 维护建议

- 修改 `SKILL.md` 后同步更新 `references/`
