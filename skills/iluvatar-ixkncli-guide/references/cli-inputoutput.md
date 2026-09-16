# 输入输出命令指南

> 本文档概述 `ixkn-cli` 的输入/输出相关 option；权威来源以 `ixkn-cli -h` 与官方手册为准。

下文说明 `ixkn-cli` 的**输入/输出相关 option**。

1. 输入有两种：**`[cuda-app] [app-args]`** 或 **`*.ixkn-rep`** 报告文件（示例：`vecAdd.ixkn-rep`）。**互斥只能选择一种**，异常输入不保证。
2. **输出分两层**（不要混为一谈）：
   - **终端可读输出**：在未使用 **`-o`** 时，在 **默认/`--page details`**、**`--csv`**、**`--page source`** 三者中**择一**（见下文优先级）。不写 **`-o` / `--csv` / `--page`** 时，终端行为等价于 **`--page details`**。
   - **写盘（`-o`）**：导出 **`.ixkn-rep`**。**有 `-o` 时终端不会出现**上述三种终端可读输出。报告默认含 **assembly**；写入 **cuda/IR** 用 **`--import-source`**（见 `cli-pagesource.md`）。**`--page source` 属终端形态**，勿与 **`-o`** 混用于命令生成。
3. **终端可读输出优先级**（实验确认）**：**`--page source` > `--csv` > 默认/`--page details`**。
4. **本 skill 命令生成（输出形态）**：
   - 用户**未说明**要 CSV、导出文件或看源码时：按**终端指标**处理，命令中**显式写 `--page details`**（与 CLI 默认等价，但便于阅读与对照步骤 5）。
   - 用户要 **CSV** → 只加 **`--csv`**（一种形态）。
   - 用户要 **`.ixkn-rep` 文件** → **`-o`**（按需 **`-f`**、**`--import-source`** 等；**勿**同条加 `--page source`）。
   - 用户要在终端看 **源码** → **`--page source`**（及 **`--print-source`** 等，见 `cli-pagesource.md`）；**勿**与 **`-o`** 同条混用。
   - 用户要 **导入已有报告** → **`-i`** + 终端形态（**`--page details`** / **`--csv`** / **`--page source`** 等，按需求选一）。
   - **不要**生成 CLI 不报错但语义含混的组合（如 **`-o` + `--csv`**、**`-o` + `--page source`**）。

## 输入分类

1. **`./vectorAdd`** 是 **`[cuda-app] [app-args]`** 的示例，此处无 **`[app-args]`**。

```bash
ixkn-cli ./vectorAdd
```

2. **`-i vecAdd.ixkn-rep`** 表示解析已有报告（路径一般为 `*.ixkn-rep`）。

```bash
ixkn-cli -i vecAdd.ixkn-rep
```

导入报告时，已 profile 的数据已固化，相关限制见下节「`-i` 专用限制」。输出侧仍受「交叉规则」与终端优先级约束（默认终端或 `--csv` 等）。

最终，根据用户需求判断输入方式，未显式说明时默认为 **`[cuda-app] [app-args]`**。

## 输出分类

1. **默认终端输出**：未指定 **`-o`**、**`--csv`**、**`--page`** 时，等价于 **`--page details`**（按 section 展示）。

```bash
ixkn-cli ./vectorAdd
```

```bash
ixkn-cli --page details ./vectorAdd
```

```bash
ixkn-cli -i vecAdd.ixkn-rep
```

```bash
ixkn-cli -i vecAdd.ixkn-rep --page details
```

2. **`--csv`**：终端 CSV 输出（与默认/details、page source 互斥，见优先级）。

```bash
ixkn-cli --csv ./vectorAdd
```

3. **`-o vecAdd`**：导出 **`.ixkn-rep`**（**写盘**；**终端无** details/csv/page source 类可读输出）。**路径可不带 `.ixkn-rep` 后缀**，工具会**自动补全**（如 `-o vecAdd` → `vecAdd.ixkn-rep`）。目标已存在且未 **`-f`** 时报错退出，例如：

```text
Error: File already exists:/path/to/file.ixkn-rep
Use "-f" to overwrite existing file
```

```bash
ixkn-cli -o vecAdd ./vectorAdd
```

4. **`-f -o vecAdd`**：导出时强制覆写已存在文件。

```bash
ixkn-cli -f -o vecAdd ./vectorAdd
```

5. **`--page source`**：终端查看源码（与 details、csv 互斥；优先级最高）。细节见 `cli-pagesource.md`。

```bash
ixkn-cli --page source ./vectorAdd
```

根据用户需求判断输出方式；导出时默认不带 **`-f`**，除非已报错或用户显式要求覆写。

## 实验确认的交叉语义（`ixkn-cli`）

以下为本仓库用例实验结论；若与 **`ixkn-cli -h`** 或新版本不一致，以工具为准。

| # | 场景 | 结论 |
|---|------|------|
| 1 | **`-i` + `--csv`** | **支持**：导入报告后可终端输出 CSV。 |
| 2 | **`-i` + `-o` / `--export-profile`** | **不支持**：CLI **报错退出**（导出与导入互斥）。 |
| 3 | **`[cuda-app]` + `-o` + `--csv`** | CLI **不报错**，但**终端无 CSV**（`-o` 屏蔽终端可读输出）。**命令生成勿同写**。 |
| 4 | **终端：`--page details`/默认 与 `--csv`** | **互斥**；同写时终端**仅 CSV**（details 不出现）。**命令生成勿同写**。 |
| 5 | **终端：优先级** | 同写多种终端形态时（如 source + csv + details），终端**只出优先级最高者**：**`--page source` > `--csv` > 默认/`--page details`**。**命令生成只选一种终端形态**。 |
| 6 | **`-o` 目标已存在且未 `-f`** | **报错退出**，提示 `Use "-f" to overwrite existing file`。 |
| 7 | **`--quiet` + `-o`** | **写盘不受影响**；`--quiet` 额外关闭终端上的 `ixkn-cli` 输出。 |
| 8 | **`[cuda-app]` + `-o` + `--import-source`** | **终端无** details/csv/source（**`-o` 屏蔽终端**）。**报告**：含 **profile 指标**（由 `--set`/`--section` 等采集）+ 默认 **assembly**；**`--import-source`** 可再写入 **cuda/IR**。**可与** filter、section、**`--print-kernel-args`** 等同条 live 导出（见 `cli-pagesource.md`）。 |
| 9 | **`[cuda-app]` + `-o` + `--page source`** | CLI **可能不报错**，但 **`--page source` 属终端形态**，有 **`-o`** 时**终端仍无 source 展示**；报告内容仍由 **`-o` / `--import-source`** 决定，**非** `--page source`。**命令生成勿同写** `-o` 与 `--page source`；要终端看源码用无 `-o` 的命令，要导出用 **#8**。 |
| 10 | **`-i` + `--quiet`** | **`-i` 路径仅有终端输出**（不得 **`-o`**，见 #2）。**`--quiet` 抑制全部终端输出**；同用 CLI **可能不报错**，但**终端无任何 `ixkn-cli` 结果**，等于无意义。**命令生成禁止** `-i` 与 `--quiet` 同写。 |
| 11 | **`-i` + 步骤 2/3（过滤类 / 指标范围类）** | **`-i` 报告已固化**，步骤 2 过滤类（`-k`、`--kernel-id`、`--filter-mode`、`-s/-c` 等）与步骤 3 指标范围类（`--set`、`--section`）**不会生效**。CLI **可能不报错**，但**不会改变**导入结果。**命令生成禁止** `-i` 与上述 live option 同写；要改指标/过滤应 **live 时 `-o` 导出** 或接受报告已有内容。 |

## 输入输出交叉规则（与上表一致）

1. **`[cuda-app]`**：
   - **终端**：**默认/details**、**`--csv`**、**`--page source`** 三选一（遵守优先级 #5）。
   - **写盘**：**`-o`**（可加 **`-f`**、**`--import-source`**）；有 **`-o`** 时**无**终端可读输出（#3、#8）。**勿**命令生成 **`-o` + `--page source`**（#9）。
   - **命令生成**：每条命令**一种输出形态**；先导出再终端看源码/CSV → **两条命令**（如 `-o`+`--import-source`，再 `-i … --page source`）。
2. **`-i *.ixkn-rep`**：仅**终端**输出（details/默认、**`--csv`**、**`--page source`** 等）；**不得** **`-o`**（#2）。终端多形态同写仍遵守优先级 #5。**命令生成禁止** **`-i` + `--quiet`**（#10）。
3. 用户需求冲突时说明原因并给替代写法。

## `-i` 专用限制

在 **`*.ixkn-rep`** 导入模式下，**不得**再使用下列 **live profile** 类 option（若与 `ixkn-cli -h` 不一致，以手册为准）：

- **步骤 2（过滤与 profile 范围）**：`--profile-from-start`、`--disable-profiler-start-stop`、`--kernel-id`、`--filter-mode`、`--kernel-name` / `-k`、`--launch-skip` / `-s`、`--launch-count` / `-c`、`--kill`、`--profile-child-processes`、`--devices`、`--target-processes-filter`
- **步骤 3（指标范围）**：`--list-sets`、`--set`、`--list-sections`、`--section`
- **步骤 4（补充）**：`--replay-mode`、`--quiet`（**`-i` 下命令生成禁止 `--quiet`**，见 #10）。**`--print-kernel-args`** 见下「`-i` 与 `--print-kernel-args`」——**勿**在 **`-i`** 命令生成里指望其改变展示。

**`-i` 与步骤 2/3（过滤类、指标范围类）**：同写 **不报错、不生效**（见实验表 #11）；命令生成 **禁止** `-i` 与 `-k` / `--kernel-id` / `--section` / `--set` 等组合。

**`-i` 与 `--print-kernel-args`**：导入报告时，kernel 实参**若已写入报告则显示、未写入则不显示**；**不再**由 **`-i` 命令上的 `--print-kernel-args`** 控制。**命令生成勿**在 `-i` 路径加 `--print-kernel-args`。要在报告里带实参，请在 **live + `-o`** 时使用 **`--print-kernel-args`**（见 `cli-addition.md`）。

**例外（步骤 5，与 `-i` 兼容）**：**`--page`**、**`--print-source`**、**`--resolve-source-file`** 可与 **`-i`** 组合（见 `cli-pagesource.md`）。

**步骤 5 中仍不适用**：**`--import-source`**（需 **`-o`**；**`-i`** 禁止 **`-o`**）。
