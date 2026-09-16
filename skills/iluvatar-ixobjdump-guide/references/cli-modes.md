# 主模式：list / dump / extract

> 权威：`ixobjdump -h`。交叉行为以本机实测为准（基线 ixToolkit v5.0.0 / `afwd` 等样本）。

## 命令骨架

```bash
ixobjdump [OPTIONS] <file>
```

- **仅一个** positional `file`；多文件会报 `argument was not expected`。
- 无 dump/list/extract option 时：仅打印 fatbin 简要头（arch/host 等），**不是**完整 SASS。

## 三类主模式

### 1. list（清单）

| option | 作用 | 典型后续 |
|--------|------|----------|
| `--lelf` / `--list-elf` | 列出 fatbin 内 ELF/ixbin 名 | `--xelf` |
| `--lIR` / `--list-IR` | 列出 IR 名 | `--xIR` |
| `--ltext` / `--list-text` | 列出设备函数 text 名 | `--xtext` / `--fun` |

`-h` 对 list 的表述：与该 flag 同写时 **其它 option 会被忽略**（用于先发现再 extract）。

**实测补充：**

| 组合 | 结果 |
|------|------|
| `--lelf --sass` | 只 list（sass 被忽略） |
| `--lIR --IR` | 只 list |
| `--ltext --sass` | **仍出 sass**（`--ltext` 未压过 `--sass`） |
| `--lelf --xelf all` | **走 extract**（extract 优先于 list） |

**命令生成：** list 命令不要再叠 dump/extract；发现与提取拆成两条。

### 2. dump（终端转储）

| option | 作用 |
|--------|------|
| `--sass` / `--dump-sass` | 设备伪汇编 |
| `--IR` / `--dump-IR` | 嵌入式 LLVM IR 文本 |
| `--elf` / `--dump-elf` | ELF section 概要 |
| `--symbols` / `--dump-elf-symbols` | ELF 符号表 |
| `--all` / `--all-fatbin` | 扩大 dump 的 fatbin 范围（配某一 dump 使用） |

**多 dump 同写（CLI 层，顺序无关）实测优先级：**

`IR` > `sass` > `elf` / `symbols`（同写时终端往往只见优先级最高者）

**命令生成：** 一条命令只选一种 dump；需要多种内容时拆多条命令。

### 3. extract（写盘）

| option | 作用 |
|--------|------|
| `--xelf TEXT` / `--extract-elf TEXT` | 提取 ELF；`all` 或局部文件名 |
| `--xIR TEXT` / `--extract-IR TEXT` | 提取 IR；`all` 或局部名 |
| `--xtext TEXT` / `--extract-text TEXT` | 提取 text 二进制编码；`all` 或局部名 |

`-h`：extract 时 **dump/list 会被忽略**。

**落盘位置（实测）：**

- 输入为**绝对路径**时，提取物常写在**输入文件同目录**（如 `afwd.MR-Linux_1.ixbin`、`afwd-_Z….MR.elf.bin`）。
- 输入为**相对路径**且在当前目录时，IR 等可能写在**当前工作目录**（如 `afwd.MR_Linux_x64.bc` / `.ll`）。

回答中应提示用户注意 cwd 与输入路径，避免找不到产物。

## 模式优先级（生成约定）

1. 用户要提取 → 只写 extract（先 list 发现则两条命令）。
2. 用户只要清单 → 只写 list。
3. 用户要看内容 → 只写一种 dump（默认 `--sass`）。
4. **禁止**生成 `list+dump`、`extract+dump`、`多 dump` 混写。

## 快速示例

```bash
# list
ixobjdump --lelf ./afwd
ixobjdump --lIR ./afwd
ixobjdump --ltext ./afwd

# dump
ixobjdump --sass ./afwd
ixobjdump --IR ./afwd
ixobjdump --elf ./afwd
ixobjdump --symbols ./afwd

# extract
ixobjdump --xelf all ./afwd
ixobjdump --xIR all ./afwd
ixobjdump --xtext all ./afwd
```
