# page 相关 option

> 本文档概述 `ixkn-cli` 的结果展示与源码（IR/汇编/CUDA）相关 option；与终端/I/O 分工见 `cli-inputoutput.md`（**步骤 1**）。

`ixkn-cli` 中用于 **终端展示** kernel 指标版式（`--page details`）或 **源码**（`--page source`），以及 **`-o` 导出** 时是否把源码写入 `.ixkn-rep`。

**与终端 / `-o` 的关系**（详见 `cli-inputoutput.md`）：

- **终端看指标**：默认等价 **`--page details`**（步骤 1 命令生成时**建议显式写出**）。
- **终端看源码**：**`--page source`**（及 **`--print-source`** / **`--resolve-source-file`**），**无 `-o`**；与 details、csv 互斥。
- **导出 `.ixkn-rep`**：**`-o`** 写盘；报告内**默认含 assembly**，并可含 **profile 指标**（由步骤 3 **`--set`/`--section`** 等决定）。要报告里再写入 **cuda/IR** 用 **`-o` + `--import-source`**。**可与**步骤 2 过滤（**`-k`** 等）、步骤 3 指标、步骤 4 **`--print-kernel-args`** **同条 live 命令**使用——一次导出同时带指标与源码。**勿**同条写 **`--page source`**（终端形态，实验表 #9）。

**与 `-i` 导入报告**：**`--page`**、**`--print-source`**、**`--resolve-source-file`** 可与 **`-i *.ixkn-rep`** 同用，**不要**再写 **`[cuda-app]`**。**`--import-source`** 仅 **live + `-o`**；**`-i`** 不能用。

```bash
ixkn-cli -i vecAdd.ixkn-rep --page details
ixkn-cli -i vecAdd.ixkn-rep --page source --print-source cuda,assembly
```

---

## option 说明

| option | 说明 |
|--------|------|
| `--page` `{details,source}` | `details`：按 section 展示指标（与步骤 3 采集范围配合）；`source`：终端看源码 |
| `--print-source TEXT` | 仅与 **`--page source`** 同用；`IR` / `assembly` / `cuda` / `cuda,assembly`；仅写 `--page source` 时默认 `assembly` |
| `--resolve-source-file TEXT` | 仅与 **`--page source`** 同用；cuda 缺源文件时按路径补充；多路径用**分号**分隔 |
| `--import-source TEXT` | 仅 **live + `-o`**；在报告已有 **assembly**（默认）基础上，是否额外写入 **cuda/IR**；**不替代** `--section`/`--set`（指标仍由步骤 3 采集） |

---

## `--page`

- **`--page details`**：终端按 section 展示 profile 结果（与未写 `--page` 的默认终端行为相同）。**指标内容由 `--section` / `--set` 决定**（步骤 3），不是本 option 采集数据。
- **`--page source`**：终端查看源码；须配合 **`--print-source`**（或接受默认 `assembly`）。对同一 kernel 多次 launch **只输出一次**源码块（源码分析，非逐 launch 轨迹）。

```bash
ixkn-cli --page source ./vectorAdd
```

---

## `--print-source`

必须与 **`--page source`** 同用。

```bash
ixkn-cli --page source --print-source cuda,assembly ./vectorAdd
```

含逗号的取值建议加引号：`--print-source 'cuda,assembly'`。

---

## 源码形态与依赖

### assembly

- **`[cuda-app]`** 与 **`-i *.ixkn-rep`** 下，**`--page source --print-source assembly`**（或仅 `--page source`）一般可直接查看汇编。

### cuda

- **`[cuda-app]`**：可执行文件须带 **debug / lineinfo**（如 `-g --device-debug`），否则终端可能 WARNING、无 CUDA 源：

```text
==WARNING== No debug info available to show CUDA-C source related to '...'.
==WARNING== Please add the -g --device-debug option when compiling ...
```

- **`-i`**：仅当 **导出时** 已写入 cuda（**`-o` + `--import-source`** 且满足编译条件）才能在报告中查看；否则 `-i` 下 `--print-source cuda` 可能无内容。

### IR

- **`[cuda-app]`**：live 无 IR 时典型 WARNING，提示用户**参考ixobjdump工具使用说明进行IR查看的处理**即可

```text
==WARNING== No code available IR code to show.
```

- **`-i`**：报告已固化；仅当 **生成报告时** 已导入 IR（**`-o` + `--import-source`** 等）才可 **`--print-source IR`** 回看。

## `--resolve-source-file`

在 **`--page source`** + **`--print-source cuda`** 时，若缺 `.cu` 等源文件，可按**文件名**在给定路径查找；**多路径用分号分隔**。**仅补 cuda**，不解决 IR 缺失。

```bash
ixkn-cli --page source --print-source cuda --resolve-source-file ./vectorAdd.cu ./vectorAdd
```

`./vectorAdd` 为 **`[cuda-app]`**，不是 `--resolve-source-file` 的取值。

---

## `--import-source`

与 **`-o` / `--export-profile`** 同用：在 **live** 导出时，除 **profile 指标**（由 **`--set`/`--section`** 等决定，并可配合 **`-k`**、**`-s`/`-c`**、**`--print-kernel-args`** 等）外，把源码写入 **`.ixkn-rep`**。

| 报告内容 | 说明 |
|----------|------|
| **指标** | 由步骤 3（及步骤 2 过滤）在 **`-o`** 导出时一并采集进报告 |
| **assembly** | **`-o` 默认**即含（无需 `--import-source`） |
| **cuda / IR** | 需 **`--import-source yes/on`** 等（且满足编译/依赖，见上文） |

**可与 filter、section、print-kernel-args 同用**（同一条 **live** 命令）：例如只 profile 某 kernel、指定 section，并在报告里带上 cuda 源与实参。

```bash
ixkn-cli -o vecAdd --import-source yes --section Instruction,SpeedOfLight \
  -k gemm --print-kernel-args --profile-child-processes ./vectorAdd
```

**勿**同条加 **`--page source`**（终端看源码；有 **`-o`** 时终端无 source 展示，见 `cli-inputoutput.md` #9）。

导出后再 **`-i`** 回看（指标或源码分终端 option）：

```bash
ixkn-cli -i vecAdd.ixkn-rep --page details
ixkn-cli -i vecAdd.ixkn-rep --page source --print-source cuda,assembly
```

---

## 使用规则

1. 用户未要求看源码/导出源码时，不必加本页 option；终端默认指标展示由步骤 1 的 **`--page details`** 负责。
2. 终端看源码：**`--page source`** + 按需 **`--print-source`**；满足上节 assembly/cuda/IR 依赖。
3. **导出报告（指标 + 源码进 `.ixkn-rep`）**：**live** 用 **`-o`**；指标靠 **`--set`/`--section`**（及步骤 2 **filter**、步骤 4 **`--print-kernel-args`** 等）**可与 `-o` 同条叠加**；报告默认含 **assembly**，要 **cuda/IR** 再加 **`--import-source`**。**不要**同条写 **`--page source`**（终端形态，非写盘）。
4. **`-i` 回看**：**`--page` / `--print-source` / `--resolve-source-file`**；**不要** **`--import-source`**。
5. **`--resolve-source-file`** 仅用于 cuda 缺文件场景；IR 缺失不能靠此 option 解决。
