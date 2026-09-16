---
name: iluvatar-ixkncli-guide
description: 根据用户对 ixKN（ixkn-cli）的需求生成可复制的命令与步骤；适用于 CUDA kernel 的 profile、SOL%/speedOfLight、解读 .ixkn-rep、以及 --section/--set/--kernel-id/-k/--launch-skip/-c 等 option 选型。在系统级分析、API 追踪与时间线、卡状态监控等场景应路由到 ixsys 或 ixsmi。
---

# ixkn-cli 命令生成

ixkn（工具命令为 `ixkn-cli`）用于分析单个 CUDA kernel，以确定其为何偏慢及如何优化。它测量 GPU 吞吐量相对理论峰值的比例（speedOfLight / SOL%），便于做瓶颈归类与针对性优化。**命令顺序严格不可变**：

```bash
ixkn-cli [OPTIONS] [cuda-app] [app-args]
```

- **[OPTIONS]**：`ixkn-cli` 的 option
- **[cuda-app]**：待分析的可执行文件
- **[app-args]**：传给该可执行文件的参数

## 版本适用声明

- 当前基线版本：**ixToolkit v5.0.0**。
- 若用户环境与该基线不一致，先提示“可能存在 option/行为差异”，并以 `ixkn-cli -h` 与官方文档为准，再生成命令。

## 何时使用

1. **适合使用本技能时**

- **触发条件**：用户要分析 CUDA kernel、解读 `ixkn`/`ixkn-cli` 输出、解释 `.ixkn-rep` 报告，或优化 GPU kernel 性能，并需要可执行的命令或步骤。
- **关键词**：`ixkn`、`ixkn-cli`、`SOL%`、`speedOfLight`、`kernel profile`、`compute-bound`、`memory-bound`、`latency-bound`、`occupancy`、`warp stalls`、`cache hit rate`、`ixkn-rep`

2. **不要使用本技能时**

- 系统级分析 → Iluvatar System Analyzer / `ixsys`
- CUDA API 追踪或 CPU–GPU 时间线 → `ixsys`
- 汇总级 kernel 执行时间以确认热点 → `ixsys`
- 无深入分析意义的卡状态/温度/功耗/显存/进程监控 → `ixsmi`

3. **ixkn 与 ixsys 对比**

| 工具 | 范围 | 开销 | 用途 |
|------|------|------|------|
| **ixsys** | 系统级 | 5–10% | 找出值得优化的 kernel |
| **ixkn** | kernel 级 | 慢 10–100 倍 | 解释该 kernel 为何慢 |

建议流程：先用 **ixsys** 找出 GPU 时间占比最高的 kernel，再用 **ixkn** 对目标 kernel 深入分析。

4. 当用户需求与本技能中 `ixkn-cli` 的用法不匹配时：明确说明原因并给出推荐工具。

## 依赖与环境限制

| 项 | 版本/条件 | 说明 |
|----|-----------|------|
| ixToolkit | v5.0.0 | 本 skill 的默认语义基线 |
| `ixkn-cli` 二进制 | 与 ixToolkit 版本匹配 | — |
| Iluvatar GPU | BI150、MR100、BI300 等 | 以实际发布说明为准 |
| svGPU | — | 当前约定：`ixkn-cli` 不在 svGPU 上使用 |

## 原则

### 参考完整性

严格的命令生成约束：**展示的每条命令须有权威来源（本 SKILL、`references/` 或官方文档）。**

1. **先引用，后解释**：option 含义须能对应到上述来源中的表述。
2. **绝不虚构 option**：无法在来源中核实时，明确说明无法生成并提示用户用 `ixkn-cli -h` 或官方文档核对。

### 术语分层（输出相关）

- **CLI 行为层**：同条命令若并写多种终端输出形态（如 `--page source`、`--csv`、`--page details`），CLI 可能不报错，但终端只显示优先级最高的一种。
- **命令生成层（本 skill）**：为避免语义含混，生成命令时始终**一条命令一种输出形态**，不生成同写多形态组合。

### 命令生成与 reference 示例

`references/` 内 bash 示例多为**最短 CLI 演示**，可不含 **`--page details`**、**`--set default`**、**`--profile-child-processes`** 等。**命令生成**须按本 SKILL 步骤 1–5 及各 reference 中的「**命令生成**」「**本 skill 约定**」执行（含显式 **`--page details`**、未指定指标时显式 **`--set default`**、live 默认 **`--profile-child-processes`** 等）。

## 工作流程

**先选路径，再决定是否走步骤：**

- **知识查询**（某指标含义、`--section` 与 `--set` 的区别、过滤器格式、某 option 作用等）：依据本文件与「查找更多信息」中的资料直接回答，**不必**逐步执行下列步骤 1–5。
- **命令生成**（指定 section、过滤 kernel、次数限制、输出格式等）：按需组合 option，**按步骤 1–5 选取**，生成完整命令。用户只要 **list**（`--list-sets` / `--list-sections`）时，在**步骤 3** 只生成列表命令并**结束**（见步骤 3 约定）。
- **命令生成 + 执行**：在用户明确要求且环境允许时，可在生成命令后执行 **可选：执行前自检** 与 **可选：实际运行命令**。

**统一前提（命令生成路径）：**

- 输入为 **`[cuda-app] [app-args]`** 时，步骤 2–5 中的 **live profile** 类 option 方可生效（步骤 5 的展示/源码 option 亦主要面向 live）。
- 输入为 **`-i *.ixkn-rep`** 时，报告已固化：**步骤 2–4 不适用**；**步骤 5** 中的 **`--page` / `--print-source` / `--resolve-source-file`** **可与 `-i` 同用**；**`--import-source`** 依赖 **`-o`**，**`-i`** 下不可用。详见 `references/cli-inputoutput.md`「`-i` 专用限制」与 `references/cli-pagesource.md`。

**步骤约定：** 各子步骤正常则继续，异常则说明原因并终止后续流程。

### 步骤 1：确定输入输出格式

目的：确定输入方式，以及**一种**输出形态（终端指标 / CSV / 源码展示，或 **`-o` 写盘**）。

| option | 说明 |
|--------|------|
| `--page details` | **终端指标**（默认输出形态）。用户未指定其它输出时，**命令生成应显式写出本项** |
| `--page` `{details,source}` | `details`：同上；`source`：终端源码（步骤 5）。**CLI 层**同写时按优先级 **source > csv > details**；**命令生成层**不与 `--csv`/其它终端形态同写 |
| `--csv` | **终端** CSV；**`-i`** 支持。与 **`-o`** 同写时**终端无 CSV** |
| `--export-profile TEXT` / **`-o TEXT`** | **写盘** `.ixkn-rep`（路径**可省略** `.ixkn-rep` 后缀，工具**自动补全**）。**终端无** details/csv/source。报告内 cuda/IR 用 **`--import-source`**（**勿** `-o`+`--page source`）。与 **`-i`** **互斥**（报错） |
| `--import-profile TEXT` / **`-i TEXT`** | 从已有报告导入；**仅终端**输出。与 **`-o`** 互斥；**命令生成禁止**与 **`--quiet`** 同用 |
| `--force-overwrite` / **`-f`** | 与 `-o` 配合，强制覆盖已存在文件 |

**`-i` 报告模式：**

- **仅终端输出**（details/`--csv`/`--page source` 等）；**不能与** `-o` / `--export-profile` **同用**（CLI 报错退出）。
- **不得**再叠加步骤 **2–4** 的 **live profile** option（含 **`--replay-mode`**、**`--quiet`**、**`--print-kernel-args`** 等；**`-i` 同写不报错但不生效**，见 `cli-inputoutput.md` 实验表 #11）；**命令生成禁止**在 **`-i`** 命令上写上述 live option；
- **命令生成禁止** **`-i` + `--quiet`**：`-i` 依赖终端展示，`--quiet` 关掉全部终端输出；CLI 可能不报错但**无任何输出**（实验表 #10）；
- **命令生成禁止** **`-i` + `--print-kernel-args`**：导入时实参**报告有则显、无则不显**，不受该 flag 控制；如果此时查看实参，导出时需 **live `-o` + `--print-kernel-args`**；
- **步骤 5** 中 **`--page` / `--print-source` / `--resolve-source-file`** **允许**与 **`-i`** 组合；**`--import-source`** 因依赖 **`-o`** 而在 **`-i`** 下不用。

**输出模型（详见 `references/cli-inputoutput.md`）：**

- **终端可读输出**（无 **`-o`** 时）：**默认/`--page details`**、**`--csv`**、**`--page source`** **三选一**；同写时 CLI 不报错，终端只出优先级最高者：**source > csv > details**。
- **`-o` 写盘**：与终端可读输出**分开**；有 **`-o`** 时**终端无** details/csv/source。报告默认 **assembly**；写入 **cuda/IR** 用 **`--import-source`**（`cli-pagesource.md`）。
- **本 skill 命令生成（输出）**：
  - **未说明**输出偏好 → **显式加 `--page details`**（终端看指标）。
  - 要 **CSV** → 只加 **`--csv`**；要 **报告文件** → **`-o`**（按需 **`--import-source`** 等）；要 **终端看源码** → **`--page source`**（步骤 5）；要 **已有报告** → **`-i`** + 对应终端 option。
  - **一种形态 per 命令**；勿 `-o`+`--csv`、`-o`+`--page source` 等。
  - **先导出、再回看**：两条命令，例如 `ixkn-cli -o vecAdd --import-source yes ./app`，再 `ixkn-cli -i vecAdd.ixkn-rep --page source --print-source cuda`（见 `cli-pagesource.md`）。
- **`--quiet`**：额外关闭终端输出；**`-o` 写盘不受影响**（实验表 #7）。

详见 `references/cli-inputoutput.md`。

### 步骤 2：确定分析过滤 option（仅 live profile）

目的：限定 **kernel 过滤、launch 采样** 与 **进程 / 多 device 作用范围**，降低开销。细节见 `references/cli-filter.md`：**段 → 名过滤 → 采样** 合称 **base profile**；**作用范围**（pcp、`--devices`、`--filter-mode` 等）决定落在哪些 **CPU 进程**、哪些 **device** 上——**各 CPU 进程配置相同**（同一条命令同一套取值），**独立实例、各自生效**。`--disable-profiler-start-stop` 优先于 `--profile-from-start`；`--devices` 先圈卡再 `--filter-mode`。

| option | 说明 |
|--------|------|
| `--profile-from-start` `{on,off}` | 程序可有**多段** Start/Stop。本项**只**控制是否在**程序头**自动开启采样：`on`（默认）从程序头可采，程序内 Stop **仍会停**、再遇 Start **再采**；`off` **不在程序头**加 Start，跟用户 Start，**Stop 仍停、再 Start 再采**。**`off` 且程序从未 Start → 可能采不到 kernel** |
| `--disable-profiler-start-stop` | 使程序内**全部** Start/Stop **失效**，profile **不再**受 Start/Stop 划定范围；**优先级高于** `--profile-from-start` |
| `--kernel-name TEXT` / **`-k TEXT`** | 在 **demangle 完整名**上过滤，**默认正则**；多 pattern 逗号分隔为 **OR**。**命令生成**：有过滤需求时默认 **`-k`**；**精确匹配默认只到标识符**：**`-k '\bNAME\b'`**（**不区分重载**，模板/非模板同名实例一并命中；排除 `afwd1` 等相近名），勿只写 `-k NAME`、勿要求用户写形参；详见 `cli-filter.md`「精确匹配的默认逻辑」。与 **`--kernel-id` 二选一**（勿同写）。若误同写，CLI **仅 `--kernel-id` 生效** |
| `--kernel-id TEXT` | **identifier 过滤**，格式 `context-id:stream-id:kernel-name:correlation_id`（省略段为通配）；各段**默认正则**。**命令生成**：有 **context-id** / **stream-id** 需求时用，与 **`-k` 二选一**。**禁止填写第 4 段 `correlation_id`，但必须保留尾部 `:`（例如 `.*:7:^foo$:`）** |
| `--launch-skip TEXT` / **`-s TEXT`** | **段范围 → 名过滤 → `-s`**：在前两步筛出的 launch 上，跳过前 N 次匹配 |
| `--launch-count TEXT` / **`-c TEXT`** | 在 **`-s`** 之后，对仍匹配的 launch **最多再 profile N 次**；多段 Start/Stop 时**同一进程内全局累计**；`global`/`per-gpu`/多进程见 `cli-filter.md` |
| `--kill` `{on,off}` | 仅与 **`-c`** 同用；`-c` 为**上限**；`on` 时记满 `-c` **提前结束**；未记满（含无匹配 kernel）则**正常结束**；默认 `off` |
| `--filter-mode` `{global, per-gpu}` | **单进程内**多 device：`global` 共用 base profile；`per-gpu` 每卡各一份。**多 CPU 进程时各进程同一 `filter-mode` 取值**，进程间独立生效 |
| `--devices TEXT` | **单进程内**圈 device；默认全部。**多 CPU 进程时各进程同一 `devices` 列表**（与 base profile、`filter-mode` 一致），进程间独立生效 |
| `--profile-child-processes` | 是否跟**子进程**；不加则仅当前进程（**工具默认不加**）。加后各子进程内仍可用 **`--filter-mode` / `--devices`** |
| `--target-processes-filter TEXT` | 按进程名筛选（**默认正则**）。**无 pcp**：仅**主进程**进待筛集合（tpf 只决定主进程是否 profile）。**有 pcp**：主、子进程均参与。**命令生成**：用户要进程名过滤且未强调只要主进程 → **默认 `pcp` + `tpf`**；用户明确只要「按名决定主进程是否 profile」→ **仅 `tpf`、不加 `pcp`** |

**本 skill 约定**：

- **`--profile-child-processes`**：live 且用户未明确拒绝时，**默认写入**并**简短说明**（防漏采子进程；单进程或缩范围可删）。**例外**：用户明确**仅主进程按名筛**（只写 **tpf**）或不要子进程 profile → **不加 `pcp`**。**`-i`** 勿默认追加。
- **`-k` / `--kernel-id`**：**默认不设置**；用户有过滤需求时再设，**二选一**——默认 **`-k`**；有 **context-id** / **stream-id** 时用 **`--kernel-id`**。勿同写（见 `cli-filter.md`）。**`-k` 精确匹配**：**一律标识符级** **`\bNAME\b`**（**不区分重载**；用户说模板/非模板版亦合并为同一 `NAME`）；多个标识符用 **`\b(?:a|b)\b`** 或逗号 OR。仅用户明确要子串/前缀时才用未加边界的 pattern，并简短提示误匹配风险。
- **`--kernel-id` 省略 `correlation_id`**：命令生成时**始终保留第 4 段占位冒号**，即使用 `context:stream:name:` 形态；例如“stream=7 且 kernel 精确匹配 foo”写成 **`.*:7:^foo$:`**，不要写成 **`.*:7:^foo$`**。
- **`--target-processes-filter`**：用户要**进程名过滤**且未强调只要主进程 → **默认 `pcp` + `tpf`**；用户明确**只按名决定主进程是否 profile** → **仅 `tpf`、不加 `pcp`**（且不适用上条默认 **pcp**）。见 `references/cli-filter.md` §4。

详见 `references/cli-filter.md`。

### 步骤 3：确定分析指标范围（仅 live profile）

| option | 说明 |
|--------|------|
| `--list-sets` | 列出所有 set；可与 **`--list-sections`** 同用；**命令生成勿与** `--set`/`--section`/`[cuda-app]` 等混写 |
| `--list-sections` | 列出所有 section；规则同上 |
| `--set TEXT` | 指定 set（CLI 可与 `--section` 并集；**命令生成与 `--section` 二选一**） |
| `--section TEXT` | 指定 section；`all` 表示全部（**命令生成与 `--set` 二选一**） |
| `--list-metrics` | 列出 section 需要的 metrics；可配 `--devices`；可选 `--section`（未指定则默认 section） |
| `--query-metrics` | 查询设备支持的 metrics；默认列全部设备，可用 `--devices` 限定 |
| `-m, --metrics TEXT` | 分析指定 metrics：逗号分隔官方名，或 **`all`** |

**本 skill 约定（步骤 3）**：

1. **用户需求是 list/query**（查集合定义或设备支持能力）：本步按需求生成 `--list-sets` / `--list-sections` / `--list-metrics` /  `--query-metrics` / ，**随即结束命令生成**——不要再带 `[cuda-app]`、步骤 2 过滤、`-o` 等 profile 路径 option。
2. **用户需求不是 list**（要 profile）：在 **profile 范围三选一** 中只选一种路径：
   - 用户**未明确**范围 → **显式 `--set default`**（步骤 1 显式 **`--page details`**）。
   - 用户要 **section 级**分析 → 只写 **`--section`**（如 `Instruction,LaunchStats` 或 `all`）；步骤 1 默认 **`--page details`**；**勿**同写 `--set`/`-m`/。
   - 用户明确要某个 **set** → 只写 **`--set TEXT`**；步骤 1 **`--page details`**；**勿**同写 `--section`/`-m`/。
   - 用户明确点名 **metrics** / **`-m all`** → 只写 **`-m`**（**`all`** 或**逗号分隔官方名**，**二选一、禁止混写**）；步骤 1 **显式 `--page details`**；名称须在 **`references/cli-metrics.md`**（§4）核对；**`-m all`** 为 metrics 层「全部」。
   - **live `-o` + `-m`**：同条只写 **`-o` + `-m`**，遵守步骤 1（有 **`-o`** 时**勿**再写 **`--page`**）；回看用 **`-i … --page details`**。
   - **自然语言 → `-m` 选型**：在文档内 **Grep/Read** `cli-metrics.md` 后再写命令；无法核实时提示用户先跑 list/query 或说明无法生成。
   - **CLI 注记**：`--set` 与 `--section` 同写时 CLI 可能取**并集**；**命令生成仍视为六选一**，不生成 `--set`+`--section` 组合。

详见 `references/cli-section.md` 与 `references/cli-metrics.md`。

### 步骤 4：补充 option（仅 live profile）

| option | 说明 |
|--------|------|
| `--replay-mode` `{kernel,application}` | section 过多时的 replay 方式；默认 `kernel` |
| `--quiet` | 抑制**全部**终端 `ixkn-cli` 输出（含 **`--csv` / `--page`**）。**`-o` 写盘不受影响**。**`-i` 路径勿同用**（终端无输出、无意义）；见 `cli-inputoutput.md` #10 |
| `--print-kernel-args` | **live** 终端展示实参；**`-o`** 写入报告；**`-i` 勿用**（报告有则显、无则不显，不受此 flag 控制） |

**本 skill 约定（步骤 4）**：**命令生成禁止** **`--print-kernel-args`** 与 **`--replay-mode application`** 同条命令（见 `cli-addition.md` 使用规则 #3）。**`-i` 路径禁止**步骤 4 全部 live option（含 **`--replay-mode`**、**`--quiet`**、**`--print-kernel-args`**；**`--quiet`** 亦见步骤 1）。

详见 `references/cli-addition.md`。

### 步骤 5：page source option（结果展示与源码）

**与步骤 1 的分工**：终端**指标**已在步骤 1 用 **`--page details`**（或 **`--csv`** / **`-o`**）确定；**本步勿再写 `--page details`**，避免与步骤 1 重复。仅当用户要**终端看源码**或**导出报告内 cuda/IR** 时使用本步 option。

目的：终端看源码，或 **live 导出**时把源码写入 **`.ixkn-rep`**（与 `references/cli-pagesource.md` 一致）。**终端看源码**：**`--page source`** + **`[cuda-app]`** 或 **`-i`**。**导出**：**`-o` + `--import-source`**（报告默认 **assembly**；`import-source` 控制是否再写入 cuda/IR）。**`-o` 可与**步骤 2 **filter**、步骤 3 **section/set**、**`--print-kernel-args`** **同条**——一次导出**指标 + 源码**。**勿** **`-o` + `--page source`** 同条。**`-i`** 不可用 **`--import-source`**。

| option | 说明 |
|--------|------|
| `--page` `{details,source}` | **`source`**：终端展示 kernel 代码（本步常用）。**`details`** 属步骤 1 终端指标，生成命令时在步骤 1 写，**勿在本步重复** |
| `--print-source TEXT` | 在 **`--page source`** 下选择展示形态：`IR`、`assembly`、`cuda`、`cuda,assembly` 等 |
| `--resolve-source-file TEXT` | 指定用于解析的源码路径；多路径用**分号**分隔 |
| `--import-source TEXT` | **live + `-o`**：在报告默认 **assembly** 外是否写入 **cuda/IR**；**可与** filter、**`--set`/`--section`**、**`--print-kernel-args`** 同条；**勿** **`-o` + `--page source`** |

详见 `references/cli-pagesource.md`。

### 可选：执行前自检（仅在将实际运行 `ixkn-cli` 时）

```bash
ixkn-cli -v
ixkn-cli -h
```

用于确认当前环境能否调用 `ixkn-cli`；**知识查询**或仅**命令生成**时不必执行。**若自检失败**（如无该命令、版本异常）：**不要**执行「实际运行命令」步骤，只输出可复制命令并提示用户先修复环境。

### 可选：实际运行命令

在用户明确要求执行且参数齐全、且自检已通过（若已做自检）时，运行步骤 1–5 拼出的完整命令（含 **`[cuda-app] [app-args]`**，或 **`-i …` + `--page` 等** 合法组合）。

## 推荐回答模板

当用户描述「我要做什么」时，建议输出：

1. **是否在 ixkn 适用范围内**；若否，说明并引导 `ixsys` / `ixsmi`。
2. **可复制命令**（占位符如 `KERNEL`、`COMMAND`、`ARGS`）。**命令生成**时：用户未要 CSV、**`-o`** 或终端源码时，**显式写 `--page details`**（终端指标，与 CLI 默认等价）。
3. **对应步骤 1–5 中的关键 option**为何这样选（一两句话）；若命令含 **`--profile-child-processes`**，按 `references/cli-filter.md` **「profile 作用范围设置」** 与 **option 应用规则 #4** **默认附带一句说明**（为何加、何时可删）。
4. 仅当用户要求且环境就绪时，再执行自检与实际运行。

**示例（命令生成）：**

- 用户：「对 vectorAdd 抓 Instruction 和 LaunchStats」  
  → `ixkn-cli --page details --profile-child-processes --section Instruction,LaunchStats ./vectorAdd`（并提示：默认跟子进程以防漏采；若确定单进程可删 `--profile-child-processes`）
- 用户：「只看名字里带 gemm 的 kernel，跳过前 2 次 launch，再 profile 1 次」  
  → `ixkn-cli --page details --profile-child-processes -k gemm --launch-skip 2 --launch-count 1 ./COMMAND ARGS`（同上提示）

## 查找更多信息

### 第一层：本文件（SKILL.md）

先在本文件中检索流程与原则。

### 第二层：本仓库 `references/`

与当前仓库内容一致的可查文件：

- `references/cli-inputoutput.md` — 输入/输出、**实验确认的交叉语义**（含 **`--quiet`+`-o` 写盘**）、**`-i` 禁用 option 清单**及 **`-i` + `--page` 等例外**
- `references/cli-filter.md` — **kernel 过滤**（profile 段、`-k`/`--kernel-id`、`-s`/`-c`、`--kill`、`--filter-mode`）与 **多进程/多 device**（`--profile-child-processes`、`--devices`、`--target-processes-filter`）及默认子进程约定
- `references/cli-section.md` — `--list-sections`、`--section`、`--set`、`--list-sets`
- `references/cli-metrics.md` — `-m` 官方 metric 名与样例输出
- `references/cli-addition.md` — `--replay-mode`、`--quiet`、`--print-kernel-args`
- `references/cli-pagesource.md` — `--page`、`--print-source`、源码与导出

**搜索方式：** 从本 skill 根目录对 `references/` 做关键词 `Grep`，只 `Read` 命中文件。
