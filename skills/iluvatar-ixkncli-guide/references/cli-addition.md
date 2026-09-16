# 补充 option

> 本文档概述 `ixkn-cli` 的杂项 profile option；权威来源以 `ixkn-cli -h` 与官方手册为准。

`ixkn-cli` 中未单独归类的 profile 相关 option。

## option 说明

| option | 说明 |
|--------|------|
| `--replay-mode TEXT:{kernel,application}` | 收集 section 时的 replay 方式；指标过多时需多次执行 kernel。默认 `kernel` |
| `--quiet` | 终端**不出现** `ixkn-cli` 的分析/报告输出，**包括** **`--page details` / `--page source`** 与 **`--csv`** 到 stdout 的内容；**不影响** **`-o` / `--export-profile`** 写入 **`.ixkn-rep`**（写盘照常） |
| `--print-kernel-args` | **live**：终端展示 kernel 实参；**`-o`**：可将实参**写入** `.ixkn-rep`。**`-i`**：仅显示报告中**已有**实参，**不受**本 flag 控制 |

1. **`--replay-mode TEXT:{kernel,application}`**：replay 形式；默认 `kernel`，也可 `application`。

```bash
ixkn-cli --replay-mode application ./vectorAdd
```

2. **`--quiet`**：开启后 **`ixkn-cli` 在终端不产生可读的 profile 结果**，**包括**：

- **`--page details`** / **`--page source`** 的版式输出；
- **`--csv`** 写到终端的 CSV 文本。

**`-o` 写盘**：**`--quiet` 不抑制** **`-o` / `--export-profile`** 生成 **`.ixkn-rep`**；仅抑制 **终端**上的 `ixkn-cli` 输出（上列 page/CSV）。适合「少打终端、仍要报告文件」的场景。

因此**不要**指望用 **`--quiet`** 仍能在终端看到 CSV 或 page；若需要 CSV 或 page 版式输出，**去掉 `--quiet`**。另：**`-o` 屏蔽终端可读输出**；**`-o` 与 `--csv` 同用不会出终端 CSV**，见 `cli-inputoutput.md` 实验表 #3、#5。

```bash
ixkn-cli --quiet -o vecAdd ./vectorAdd
```

3. **`--print-kernel-args`**：

   - **`[cuda-app]` live**：在终端结果中，展示 kernel 名时**同时打印**运行时实参。
   - **`[cuda-app]` + `-o`**：可将 kernel 实参**一并写入**导出的 `.ixkn-rep`（与写盘同用）。
   - **`-i *.ixkn-rep`**：报告里**若已含**实参则展示，**若无则不展示**；**`-i` 命令上的 `--print-kernel-args` 不改变行为**（CLI 可能不报错但无意义）。**命令生成勿**在 `-i` 路径加此 flag；需要报告带实参时在导出时用 **`-o` + `--print-kernel-args`**。

```bash
ixkn-cli --print-kernel-args ./vectorAdd
ixkn-cli -o vecAdd --print-kernel-args ./vectorAdd
```

## 使用规则

1. 默认不指定上述 option，除非用户有明确需求说明。
2. **`--quiet` 与 `-i`**：**`-i` 路径只有终端输出**；**`--quiet` 会关掉全部终端输出**。同用 CLI **可能不报错**，但**没有任何可见结果**。**命令生成禁止** `-i` 与 `--quiet` 同写（见 `cli-inputoutput.md` 实验表 #10）。**`[cuda-app]` + `-o` + `--quiet`** 仍可用（写盘不受影响）。
3. **`--print-kernel-args`** 与 **`--replay-mode application`** **互斥**：**命令生成禁止**同条命令同时使用；若用户需求同时涉及二者，说明冲突并请其二选一或分两次运行。
4. **`--replay-mode`** 默认 `kernel`；仅当用户明确要 application 级 replay 时再写 `application`。
