---
name: iluvatar-ixobjdump-guide
description: 根据用户对 ixobjdump 的需求生成可复制命令与步骤；适用于从 CUDA/主机二进制提取或查看 SASS、ELF、IR、符号表，以及 --sass/--IR/--elf/--lelf/--xelf/--fun/--arch 等 option 选型。对标 cuobjdump。在 kernel 性能指标、系统级 trace、卡状态监控等场景应路由到 ixkn、ixsys 或 ixsmi。
---

# ixobjdump 命令生成

`ixobjdump` 从 CUDA/主机二进制中提取信息并以可读格式展示（设备伪汇编 / ELF / 嵌入式 IR 等）。**每次只接受一个输入文件**，命令骨架：

```bash
ixobjdump [OPTIONS] <file>
```

- **[OPTIONS]**：`ixobjdump` 的 option
- **`<file>`**：主机可执行文件、目标文件、库，或 cubin/相关二进制（**仅一个**）

## 版本适用声明

- 当前基线版本：**ixToolkit v5.0.0**（本 skill 以现场 `ixobjdump -h` / `-V` 为最终依据），**与 `-h` 冲突时以 `-h` 为准**。

## 何时使用

1. **适合使用本技能时**

- **触发条件**：用户要从 CUDA/主机二进制看 SASS/汇编、ELF、IR、符号，或提取 `.ixbin` / IR / text 段文件，需要可复制的 `ixobjdump` 命令。
- **关键词**：`ixobjdump`、`cuobjdump`、`sass`、`伪汇编`、`fatbin`、`cubin`、`ixbin`、`--IR`、`--elf`、`--lelf`、`--xelf`、`--fun`、反汇编、设备码

2. **不要使用本技能时**

- CUDA kernel 性能指标 / SOL% / section → `ixkn-cli`（**iluvatar-ixkncli-guide**）
- 系统级 API/时间线 / `.ptrace` → `ixsys`（**iluvatar-ixsys-guide**）
- 卡状态/温度/功耗/显存/进程 → `ixsmi`

3. **与相关工具对比**

| 工具 | 作用 | 典型输出 |
|------|------|----------|
| **ixobjdump** | 静态读二进制里的设备码/IR/ELF | 终端 dump 或提取出的文件 |
| **ixkn-cli** | 运行时 kernel 性能分析 | `.ixkn-rep`、SOL%/metrics |
| **ixsys** | 运行时系统/API 时间线 | `.ptrace` 等 |

## 依赖与环境限制

| 项 | 版本/条件 | 说明 |
|----|-----------|------|
| ixToolkit | v5.0.0 | 默认语义基线 |
| `ixobjdump` 二进制 | 与工具栈匹配 | 安装sdk设置环境变量后可用 |
| 输入 | 单个文件 | 多文件会报错（`-h`：`file` 为单个 positional） |

## 原则

### 参考完整性

1. **先引用，后解释**：option 语义须能对应到本 SKILL、`references/`、`ixobjdump -h`。
2. **绝不虚构 option**：无法核实时说明无法生成，并提示用户用 `ixobjdump -h` 核对。

### 命令生成：一条命令一种主模式

| 主模式 | 代表 option | 说明 |
|--------|-------------|------|
| **list** | `--lelf` / `--lIR` / `--ltext` | 只列清单，便于后续 extract |
| **dump** | `--sass` / `--IR` / `--elf` / `--symbols` | 终端可读转储 |
| **extract** | `--xelf` / `--xIR` / `--xtext` | 写文件到磁盘 |

- **命令生成层**：每条命令只选 **一种主模式**（必要时再加过滤 option）。
- **CLI 层**若同写多模式：行为见 `references/cli-modes.md`（extract 通常压过 list/dump；多种 dump 并存时终端往往只出优先级最高者）。为避免语义含混，**本SKILL禁止**生成多主模式混写。

## 工作流程

**先选路径：**

- **知识查询**（某 option 含义、伪汇编指令、与 cuobjdump 对照等）：依据本文件与「查找更多信息」直接回答，**不必**逐步走命令生成步骤。
- **命令生成**：按步骤 1–3 选取，生成完整命令。
- **命令生成 + 执行**：仅在用户明确要求且环境允许时，做自检并实际运行。

### 步骤 1：确定主模式（list / dump / extract）

目的：先决定用户要「列清单」「终端看内容」还是「提取文件」。

| 用户意图 | 生成 option | 备注 |
|----------|-------------|------|
| 列出内嵌 ELF / ixbin | `--lelf` | 常作 `--xelf` 前的发现步骤 |
| 列出 IR | `--lIR` | 常作 `--xIR` 前的发现步骤 |
| 列出设备函数 text 名 | `--ltext` | 常作 `--xtext` / `--fun` 前的发现步骤 |
| 看设备伪汇编（SASS） | `--sass` | 最常见 dump |
| 看嵌入式 LLVM IR | `--IR` | |
| 看 ELF section 概要 | `--elf` | |
| 看 ELF 符号表 | `--symbols` | |
| 提取 ELF/ixbin | `--xelf TEXT` | `all` 或局部名；路径见 modes 文档 |
| 提取 IR 二进制/文本 | `--xIR TEXT` | `all` 或局部名 |
| 提取 text 编码 | `--xtext TEXT` | `all` 或局部名（以 `-h` 为准） |
| 未说明、只给了文件 | 默认 **`--sass`**，并简短说明可用 `--ltext`/`--IR` 等 | |

**无 option 时 CLI 行为**（实测）：仅打印 fatbin 的简要 arch/host/IR 头信息，**不含**完整 SASS；命令生成不要依赖“裸跑文件”当默认分析。

详见 `references/cli-modes.md`。

### 步骤 2：过滤与范围（可选）

在 dump/extract 上缩小范围；list 通常单独使用。

| option | 说明 |
|--------|------|
| `--fun TEXT` / `--function TEXT` | 按**设备函数名**过滤；支持逗号分隔**多个正则**。匹配是对 **mangled 全名** 的正则（实测：裸写 `afwd` 可能找不到，需 `.*afwd.*` 或完整 `_Z4afwdPfS_S_`） |
| `--findex TEXT` / `--function-index TEXT` | 按函数序号过滤；支持单个正则或逗号分隔数字。序号从 **1** 起；`0` 非法 |
| `--arch TEXT` / `--gpu-architecture TEXT` | 只展示指定架构；逗号分隔多值。允许：`ivcore11`,`ivcore20`,`ivcore30`。**命令生成**：先建议 `--lelf`/`--IR` 头确认实际 arch，再写 `--arch`；对不上的 arch 可能导致异常/崩溃（实测） |
| `--all` / `--all-fatbin` | dump 全部 fatbin 段；默认优先可执行 fatbin |

**本 skill 约定：**

- 用户给 demangled 名（如 `afwd`）且要 `--sass`：默认生成 **`--fun '.*afwd.*'`**（或先 `--ltext` 再精确 mangled 名），并一句说明是 mangled 全名正则。
- `--fun` 与 `--findex`：**有明确名字用 `--fun`**；用户说“第 N 个函数”用 `--findex`。一般**勿同写**（CLI 可能同时生效造成困惑）。
- 多架构 fatbin 且用户指定平台时再加 `--arch`。

详见 `references/cli-filter.md`。

### 步骤 3：辅助 option

| option | 说明 |
|--------|------|
| `--optf TEXT` / `--options-file TEXT` | 从文件读入额外 CLI 选项 |
| `-h` / `--help` | 帮助 |
| `-V` / `--version` | 版本（**不是** `-v`） |

### 可选：执行前自检（仅在将实际运行时）

```bash
ixobjdump -V
ixobjdump -h
```

自检失败则**不要**执行用户目标命令，只输出可复制命令并提示修复环境（如 `source <sw_home>/enable`）。

### 可选：实际运行命令

用户明确要求执行、参数齐全、自检通过后，运行步骤 1–3 拼出的完整命令。

## 推荐回答模板

1. **是否在 ixobjdump 适用范围内**；若否，引导到 ixkn / ixsys / ixsmi。
2. **可复制命令**（未知路径用占位符）。
3. **一两句说明**主模式与过滤为何这样选（尤其 `--fun` 正则、`--arch`、extract 落盘位置）。
4. 仅当用户要求且环境就绪时，再自检并执行。

**示例：**

- 「看 `./afwd` 的 SASS」→ `ixobjdump --sass ./afwd`
- 「只看名字带 afwd 的函数汇编」→ `ixobjdump --fun '.*afwd.*' --sass ./afwd`
- 「先列出内嵌 ELF，再全部提取」→ `ixobjdump --lelf ./afwd` ，再 `ixobjdump --xelf all ./afwd`
- 「提取 IR」→ `ixobjdump --xIR all ./afwd`

## 查找更多信息

### 第一层：本文件（SKILL.md）

### 第二层：`references/`

- `references/cli-modes.md` — list / dump / extract 及交叉行为（含实测）
- `references/cli-filter.md` — `--fun` / `--findex` / `--arch` / `--all`
- `references/cli-options.md` — 与 `-h` 对齐的选项全表

**搜索方式：** 从本 skill 根目录对 `references/` 做关键词 `Grep`，只 `Read` 命中文件。
