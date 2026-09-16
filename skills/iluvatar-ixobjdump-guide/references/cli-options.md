# ixobjdump 选项全表（对齐 `-h`）

> 生成命令前若环境可用，优先以本机 `ixobjdump -h` 复核。下表对应当前基线帮助输出；与 `docs/ixobjdump440` 冲突时以本表 / `-h` 为准。

## Usage

```text
ixobjdump [OPTIONS] [file]
```

## Options

| 长选项 | 短/别名 | 参数 | 说明 |
|--------|---------|------|------|
| `--help` | `-h` | — | 帮助 |
| `--version` | `-V` | — | 版本与编译信息（**不是** `-v`） |
| `--all-fatbin` | `--all` | — | Dump 全部 fatbin 段；默认优先可执行 fatbin，否则 relocatable |
| `--dump-elf` | `--elf` | — | Dump ELF Object sections |
| `--dump-elf-symbols` | `--symbols` | — | Dump ELF symbol names |
| `--dump-IR` | `--IR` | — | Dump 所列设备函数的 IR |
| `--dump-sass` | `--sass` | — | Dump 所列设备函数的 sass/伪汇编 |
| `--extract-elf` | `--xelf` | `TEXT` | 按局部名提取 ELF；`all` 提取全部。可先 `--lelf`。同写时忽略 dump/list |
| `--extract-IR` | `--xIR` | `TEXT` | 按局部名提取 IR；`all` 提取全部。可先 `--lIR`。同写时忽略 dump/list |
| `--extract-text` | `--xtext` | `TEXT` | 按局部名提取 text 二进制编码；`all` 提取全部。可先 `--ltext`。同写时忽略 dump/list |
| `--function` | `--fun` | `TEXT` | 设备函数名过滤；逗号分隔多个正则 |
| `--function-index` | `--findex` | `TEXT` | 设备函数序号过滤；单个正则或逗号分隔数字 |
| `--gpu-architecture` | `--arch` | `TEXT` | 架构过滤；逗号分隔。允许：`ivcore10`,`ivcore11`,`ivcore20`,`ivcore30`,`ivcore40` |
| `--list-elf` | `--lelf` | — | 列出 fatbin 中 ELF；`-h` 称其它 option 忽略（便于后续 `-xelf`） |
| `--list-IR` | `--lIR` | — | 列出 IR；同上，便于 `-xIR` |
| `--list-text` | `--ltext` | — | 列出 text/设备函数名；便于 `-xtext` |
| `--options-file` | `--optf` | `TEXT` | 从文件读入额外选项 |

## 相关 reference

- 模式与交叉：`cli-modes.md`
- 过滤：`cli-filter.md`
