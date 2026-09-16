# iluvatar-ixobjdump-guide

`iluvatar-ixobjdump-guide` 是面向 `ixobjdump` 的 Agent Skill：把用户自然语言需求转换为可复制命令，并说明主模式（list / dump / extract）与过滤选项的选择依据。

覆盖重点：

- 从 CUDA/主机二进制查看 SASS、IR、ELF、符号表
- 列出并提取 fatbin 内 ELF / IR / text（`--lelf`/`--xelf`、`--lIR`/`--xIR`、`--ltext`/`--xtext`）
- 按函数名/序号/架构过滤（`--fun`/`--findex`/`--arch`）
- 与 `cuobjdump` 对标场景下的命令选型

当前基线：`ixToolkit v5.0.0`（以现场 `ixobjdump -h` / `-V` 为准）。

## 一键安装（推荐）

见 [从仓库本地安装](../../README.md#从仓库本地安装)。

## 工具对照表

| 工具 | 用户级安装位置 | 显式触发 |
|---|---|---|
| Cursor / Codex CLI / Copilot | `~/.agents/skills/iluvatar-ixobjdump-guide/` | `/iluvatar-ixobjdump-guide <问题>`（Cursor）/ `$iluvatar-ixobjdump-guide <问题>`（Codex CLI）/ 对应技能触发语法（Copilot） |
| Claude Code | `~/.claude/skills/iluvatar-ixobjdump-guide/` | `/iluvatar-ixobjdump-guide <问题>` |

## 如何触发

1. 显式触发（推荐）：`/iluvatar-ixobjdump-guide <你的问题>`
2. 隐式触发：直接自然语言提问，模型通过 `SKILL.md` 的描述自动匹配

## 使用示例

- “帮我看 `./afwd` 的设备汇编。”
- “先列出这个二进制里的 ELF，再全部提取出来。”
- “只 dump 名字里带 `afwd` 的函数的 SASS。”
- “把 IR 提取成文件。”

## 关键约束

- 每次只分析 **一个** 输入文件
- 命令生成：**一条命令一种主模式**（list / dump / extract）
- 未说明意图时默认 **`--sass`**
- `--fun` 按 **mangled 全名正则**；短名默认 `.*NAME.*`
- 版本开关 **`-V`**；不要生成文档残留的 `--disassemble-by-llvm`
- 与 `-h` 冲突时以现场帮助为准

## 单一事实来源（SoT）

- 规则语义与行为约束：以 `SKILL.md` 为准
- 选项字面量：以本机 `ixobjdump -h` 与 `references/cli-options.md` 为准
- 使用说明与安装：以本 `README.md` 为准

## 维护建议

- 修改规则前先跑 `ixobjdump -h` / `-V` 核对
- 修改 `SKILL.md` 后同步 `references/`
- 文档与 `-h` 冲突时更新 skill，并在 SKILL「版本适用声明」中记录滞后点
