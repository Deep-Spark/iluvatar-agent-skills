# profile 过滤与范围命令指南

> 本文档概述 `ixkn-cli` 的 **kernel 过滤**（名称、launch 次数、`--filter-mode`）与 **进程/多 device 范围**（子进程、指定 GPU、进程名筛选）相关 option；权威来源以 `ixkn-cli -h` 与官方手册为准。与 **`SKILL.md` 步骤 2** 表格摘要对应；细节以本文为准。

`ixkn-cli` 在限定「分析哪些 kernel、在哪些进程/device 上分析」时用到的 option。

**叠放顺序（先生效 → 后生效）**：

1. **profile 段过滤范围**（时间/Start-Stop 窗口）
2. **profile 过滤规则**（`-k` / `--kernel-id`，在段范围之后筛 kernel 名/id）
3. **profile 采样规则**（`-s` / `-c` / `--kill`，对上述仍匹配的 launch 计数）
4. **profile 作用范围**（进程集合、`--devices`、`--filter-mode` 等，决定 **base profile** 落在哪些 CPU 进程、哪些 device）

前三步合称 **base profile**（同一套段/名/采样配置）；第 4 步决定该 **base profile** 以及 **`--devices` / `--filter-mode`** 等作用范围 option 落在哪些 CPU 进程、哪些 device 上——**各被 profile 的 CPU 进程使用完全相同的一套配置**，在各进程内**独立实例、各自生效**（计数等进程间不共享）。

## profile 段过滤范围设置

被测程序内可有**多段** `cudaProfilerStart`/`Stop`（及 `cuProfilerStart`/`Stop`）围成的采样窗口。下列两个 option **只影响「是否在程序头自动开始采样」与「Start/Stop 是否还能划定范围」**，不改变「**遇 Stop 会停、之后再遇 Start 会再采**」这一基本行为（在 Start/Stop **仍生效**时）。

### `--profile-from-start` `{on, off}`

**只控制是否在程序开始时由工具侧开启采样起点**（例如在程序头注入/等价于 `cudaProfilerStart`），**不**改变程序里各次 **Stop** 是否生效。

| 取值 | 含义 |
|------|------|
| **`on`（默认）** | 从**程序头**即进入可采样状态（工具在启动侧开启采样）；程序内用户自设的 **Start/Stop 仍有效**：遇 **Stop** 仍**停止**采样，之后再次遇到 **Start** 再**恢复**采样。 |
| **`off`** | **不在程序头**自动添加 Start；**遵循**程序里用户插入的 **Start** 才开始采样。**Stop 仍会停止**采样，后续再遇 **Start** 再采。程序可有**多段** Start–Stop。 |

`off` 且程序**从未**调用 Start 时，可能采不到任何 kernel，生成命令时须提示。

```bash
ixkn-cli --profile-from-start off ./vectorAdd
```

### `--disable-profiler-start-stop`

使被测程序中**全部** `cudaProfilerStart`/`Stop` 与 `cuProfilerStart`/`Stop` **失效**：profile 范围**不再**受程序内任意 Start/Stop 控制（多段 Start–Stop 均不起划定作用）。

**与 `--profile-from-start` 同写时**：**`--disable-profiler-start-stop` 优先级更高**——以「Start/Stop 全部失效」为准；`on`/`off` 仅在与 disable **未同写**时按上表理解。

```bash
ixkn-cli --disable-profiler-start-stop ./vectorAdd
```

默认不启用；仅用户显式要求时使用。

## profile 过滤规则设置

在 **profile 段过滤范围** 的基础上，设置基于 kernel 名的过滤；**二选一**，不要与下文 采样/作用范围混淆。

### kernel 名过滤（`-k` / `--kernel-name`）

1. **`--kernel-name TEXT`**：只按 kernel **名称**过滤，等效 **`-k TEXT`**。多个 pattern 用逗号分隔为 **OR**；每个 pattern **默认按正则**匹配。

#### 匹配对象：demangle 后的完整名

**`-k` / `--kernel-id` 第 3 段** 均针对工具展示的 **demangle 后完整 kernel 名**（含模板实参、参数类型等），**不是**短 mangled 名。例如：

| 示例名 |
|--------|
| `afwd(float*, float*)` |
| `afwd1(float*, float*)` |
| `afwd<2,4>(float*, float*)` |

匹配在**整段 demangle 字符串**上进行；**本 skill 的「精确匹配」默认只到标识符粒度**，用户**只需提供 kernel 标识符**（函数名），**不必手写 `(形参)` 或模板实参**，也**不区分重载**。

#### 「精确匹配」的默认逻辑（标识符级，不区分重载）

用户说「精确匹配 `afwd`」「分析模板/非模板的 `afwd`」「只要 afwd 不要 afwd1」等，**一律**按**精确匹配某一个标识符**处理，生成 **`-k '\bNAME\b'`**：

- **会命中**：该标识符在 demangle 名中的**全部实例**——非模板 `afwd(float*, float*)`、模板实例 `afwd<2,4>(float*, float*)`、其它同名 overload，只要整段名里出现标识符 `afwd` 即匹配。
- **不会命中**：标识符不同的名，如 `afwd1(...)`（`-k afwd` 裸写会误匹配到 `afwd1`，故须 `\b`）。
- **不区分重载**：**不能**用默认精确匹配只留 `afwd<2,4>(...)` 而排除 `afwd(float*, float*)`；用户若表述为「模板版 afwd / 非模板 afwd」，仍合并为**同一个标识符 `afwd`**，生成 **`-k '\bafwd\b'`**，并向用户说明会覆盖该名下的全部 overload。

词边界 **`\b`** 的作用：在整段 demangle 字符串上锁定**标识符** `NAME`，避免子串误匹配（如 `afwd` 命中 `afwd1`），**不是**用来区分模板与非模板或不同签名。

#### 本 skill 命令生成：`-k` 选型逻辑

| 用户意图 | 命令生成写法 | 说明 |
|----------|--------------|------|
| **未提过滤** | **不写** `-k` | 默认不设置名过滤 |
| **精确匹配**（点名某一 kernel / 区分相近名 / 含模板与非模板） | **`-k '\bNAME\b'`** | **默认精确匹配路径**；不区分重载，见上节 |
| **精确匹配多个标识符**（同时要 `afwd` 与 `bfwd`） | **`-k '\b(?:afwd|bfwd)\b'`** 或 **`-k '\bafwd\b','\bbfwd\b'`** | 各标识符均不区分其下 overload |
| **前缀/子串模糊**（用户明确要「名字里含 gemm」等） | **`-k 'gemm'`** 等 | 子串正则；**须向用户说明**可能误匹配更长名（如 `gemm` 命中 `gemm_v2`） |

**要点**：**不要**只写 `-k afwd`（子串误匹配）；**不要**要求用户补全 `(形参)` 或按模板实参拆分；用户未明确要求子串/前缀时，精确匹配**只**用 **`\bNAME\b`**。

#### 示例

精确匹配标识符 `afwd`（**不区分重载**：非模板 `afwd(float*, float*)`、模板 `afwd<2,4>(float*, float*)` 均命中；**不含** `afwd1(...)`）：

```bash
ixkn-cli -k '\bafwd\b' ./test
```

同时精确匹配 `afwd` 与 `bfwd`：

```bash
ixkn-cli -k '\b(?:afwd|bfwd)\b' ./test
```

区分 `softmaxWithOneWarp` 与 `softmaxWithOneWarpRepeat`（精确匹配前者）：

```bash
ixkn-cli -k '\bsoftmaxWithOneWarp\b' ./cetest
```

用户**明确**要前缀/子串时（可能误匹配更长名，生成时简短提示）：

```bash
ixkn-cli -k 'gemm' ./test
```

### identifier 过滤（`--kernel-id`）

2. **identifier 过滤**，格式为 **`context-id:stream-id:kernel-name:correlation_id`**（各段省略时为通配）。相比 **`-k`** 可进一步限定 context、stream。**第 3 段 `kernel-name` 同样按 demangle 完整名、默认正则**匹配；精确匹配某一标识符时与 **`-k`** 相同，第 3 段写 **`\bNAME\b`**（如 **`::\bafwd\b:`**），见上表。**各段默认正则**。

**禁止在生成命令中使用第 4 段 `correlation_id`**：`correlation_id` 仅保证自增、**不具有可依赖的 launch 计数语义**。用户明确要求按 correlation 过滤时，说明不可用，**命令中严格禁止包含该段**。

```bash
ixkn-cli --kernel-id ::afwd: ./device_scan
```

示例：

- `'::foobar:'` —— 所有 context、所有 stream、kernel 名匹配 `foobar`（末尾 `:` 表示不写第 4 段）
- `':7:foo:'` —— context 通配、`stream` 段匹配 `7`、kernel 名匹配 `foo`

```bash
ixkn-cli --kernel-id ':7:foo:' ./app
```

### 两种名过滤的关系

3. **`-k` / `--kernel-name` 与 `--kernel-id`** 分两层理解（不矛盾）：

   - **CLI 行为（同一条命令里两者都写）**：CLI **不报错**，但**仅 `--kernel-id` 生效**，**`-k` 被忽略**。
   - **本 skill 命令生成**：**默认不设置**名过滤（不写 **`-k`** / **`--kernel-id`**）。用户有 kernel 过滤需求时再设，**二选一、只写一个**——默认 **`-k`**（按 kernel 名）；有 **context-id** / **stream-id** 需求时用 **`--kernel-id`**（格式见上文）。**勿**同条命令同时写两者。

### shell 引号（命令生成）

过滤 pattern 传给 shell 时，含 **`^` `,` 空格 `:` `*` `()`** 等字符的取值**建议加引号**，避免被 shell 拆分或展开：

| 场景 | 建议 |
|------|------|
| **`-k`** 含正则元字符 | **单引号**包裹每个 pattern，如 `-k '\bafwd\b'`、`-k '\b(?:afwd|bfwd)\b'`；demangle 名含 `,` 时勿与多 pattern 的逗号 OR 混淆 |
| **`--kernel-id`** 含 `:` | **单引号**包裹整段，如 `--kernel-id '::foobar:'`、`--kernel-id ':7:foo:'` |
| 仅简单字母数字 kernel 名 | 可不加引号，如 `-k gemm` |

生成命令时**优先单引号**（减少 `$`、`` ` ``、`\` 被 shell 解释）；含双引号或变量的 pattern 再改用双引号并转义。

## profile 采样规则设置

**生效顺序**：**profile 段范围** → **名过滤**（`-k` / `--kernel-id`）→ **`-s` / `-c` / `--kill`**（在前两步筛出的 launch 上再采样）。段范围含义见上文「profile 段过滤范围设置」。

**`-s` / `-c` 与作用范围**（在上述顺序之后、按进程/device 计数）：

- **多段 Start～Stop**：同一 **CPU 进程**内，`-s`/`-c` 在可采样时段内匹配到的 launch 上**全局累计**（跨段合计；段间不计入的 launch 也不计入次数）。
- **`--filter-mode global`**：单进程内多卡**合计**计 `-s`/`-c`。
- **`--filter-mode per-gpu`**：每 device **各计** N 次。
- **`--profile-child-processes`**：每个被 profile 的 CPU 进程**各自独立**计数。

1. **`--launch-skip TEXT`**：跳过前 N 次匹配的 launch，等效 **`-s TEXT`**。

```bash
ixkn-cli -s 1 ./vectorAdd
```

2. **`--launch-count TEXT`**：在 **`-s`** 生效后，对仍匹配的 launch **最多再 profile `-c` 次**，等效 **`-c TEXT`**。

```bash
ixkn-cli -c 1 ./vectorAdd
```

3. **`--kill {on, off}`**：仅可与 **`--launch-count`（`-c`）** 同用。`-c` 为在 `-s` 之后**最多再 profile 的次数上限**（不是「必须凑满」）。`kill` 为 `on` 时：**记满 `-c`** 后**提前结束**被测进程；若在程序自然结束前**从未记满 `-c`**（含**无匹配 kernel**、匹配次数不足），则**随程序正常结束**，不会为凑满 `-c` 而额外等待。默认 `off`。

```bash
ixkn-cli --kill on -c 1 ./vectorAdd
```

## profile 作用范围设置

**profile 段**、**profile 过滤规则** 与 **profile 采样规则** 共同构成 **base profile**；本节规定 **base profile** 在哪些 **CPU 进程**、哪些 **device** 上生效。

**本节四个 option 彼此可正交叠加**（在 CLI 允许的前提下自由组合），例如 `--filter-mode per-gpu` + `--devices 0,1` + `--profile-child-processes` + `--target-processes-filter`。它们也可与上文 **base profile** 各层（段 / 名过滤 / 采样）**正交叠加**。

**分工说明（便于理解，非互斥）**：

- **`--profile-child-processes`（pcp）**：开启后，每个纳入 profile 的 **CPU 进程** 各有一份 **base profile** 实例；**进程间配置一定相同**（段/名/采样及 **`--devices` / `--filter-mode`** 等同一条命令上的取值），**独立生效**（状态与计数不共享）。
- **`--devices` × `--filter-mode`（先 devices，后 filter-mode）**：在**单个 CPU 进程**内，先用 **`--devices`** 限定哪些 **device** 参与；再用 **`--filter-mode`** 决定这些 device 上是一份 **base profile**（`global`）还是**每张卡各一份**（`per-gpu`）。
- **`--target-processes-filter`（tpf）**：按进程名决定**哪些 CPU 进程**应用 base profile；各进程内再按 devices / filter-mode 执行（命令生成见 §4：默认 tpf+pcp，仅主进程按名筛时只写 tpf）。

1. **`--filter-mode TEXT:{global, per-gpu}`**：在**当前 CPU 进程**内、`--devices` 圈定的 device 上：`global` → 多卡**共用**一份 base profile；`per-gpu` → **每张卡各一份** base profile。默认 `global`。**所有被 profile 的 CPU 进程**共用**同一条命令上的同一 `filter-mode` 取值**，在各进程内分别生效。

```bash
ixkn-cli -c 1 -s 1 --filter-mode per-gpu ./vectorAdd
```

2. **`--devices TEXT`**：在**当前 CPU 进程**内，**先**限定 base profile 只作用于所列 **device** 上的 kernel（未列出的 device **不** profile）；默认**全部** device。再与 **`--filter-mode`** 决定这些 device 上共用或 per-gpu 拆分。**所有被 profile 的 CPU 进程**共用**同一条命令上的同一 `devices` 列表**（与 base profile、`filter-mode` 一致），在各进程内**独立生效**；未列出 device 上的 kernel **不在** profile 范围内。

```bash
ixkn-cli -c 1 -s 1 --filter-mode per-gpu --devices 0,1 ./vectorAdd
```

3. **`--profile-child-processes`**（**pcp**）：是否对 CPU 侧**子进程** profile。**工具默认不加** → 仅 **主进程** 被 profile；加则子进程也纳入 profile（各子进程内仍可配合 **`--filter-mode` / `--devices`**）。

```bash
ixkn-cli -c 1 -s 1 --filter-mode per-gpu --devices 0,1 --profile-child-processes ./vectorAdd
```

4. **`--target-processes-filter TEXT`**（**tpf**）：对**进程名**过滤（**默认正则**，语义类似 **`-k`**）；符合者**独立**应用 base profile（其内仍可用 **`--filter-mode` / `--devices`**）。

   - **CLI 行为（未加 pcp）**：待筛集合里**只有主进程**——用 tpf 匹配**主进程名**，决定**是否**对主进程应用 base profile；**子进程不进**待筛集合，tpf **不能**按名筛子进程。
   - **CLI 行为（已加 pcp）**：主、子进程**均**进待筛集合，tpf 决定哪些进程应用 base profile（可筛到子进程名）。
   - **本 skill 命令生成**：
     - 用户要**按进程名过滤**（需写 **tpf**）且**未强调**只要主进程时 → **默认同时写 `pcp` + `tpf`**（否则筛不到子进程）。
     - 用户**明确**「只按名决定是否 profile **主进程**」/「只 profile 主进程、按进程名」→ **只写 `tpf`，不加 `pcp`**（有意利用「未加 pcp 时仅主进程进待筛集合」）；此情形下**亦勿**套用 live 默认追加 **`pcp`**（见应用规则 #4）。

```bash
ixkn-cli --profile-child-processes --target-processes-filter Add,Mul --filter-mode per-gpu --devices 0,1 ./vectorAdd
```

5. **组合小结**：

   - **CPU 进程间**：**配置一定相同**（同一条命令上的 base profile、**`--devices`**、**`--filter-mode`** 等取值一致）；各进程**独立实例、各自生效**（profile 状态与 `-s`/`-c` 计数**不**跨进程共享）。
   - **单进程内**：`--devices` **先**圈 device → `--filter-mode` **再**定共用或 per-gpu；主进程与各子进程（在 pcp 开启且被纳入 profile 时）适用**同一套** devices / filter-mode / base profile。
   - **`-s`/`-c`/`--kill`**：**段 → 名 → 采样**；`global` / `per-gpu` / 多进程见采样节。
   - 与 `ixkn-cli -h` 不一致时以工具为准。

## option 应用规则

1. **profile 段过滤范围**：默认不设置（等价 **`--profile-from-start on`**）。`off`：不在程序头自动 Start，跟用户 Start/Stop（多段）；**Stop 仍停、再 Start 再采**。`--disable-profiler-start-stop`：全部 Start/Stop 失效，**优先级高于** `--profile-from-start`。`off` 且程序无 Start 时须提示可能采不到 kernel。
2. **profile 过滤规则**：
   - **命令生成**：**默认不设置** **`-k`** / **`--kernel-id`**。用户有过滤需求时再设：**二选一**——默认 **`-k`**；有 **context-id** / **stream-id** 需求时用 **`--kernel-id`**（格式见上文）；**勿同条命令同时写两者**。
   - **CLI 行为**：若用户误将两者都写在同一条命令里，**仅 `--kernel-id` 生效**（见 §「两种名过滤的关系」）。
   - **`-k`**：在 **demangle 完整名**上匹配；**精确匹配默认只到标识符**（**`\bNAME\b`**，**不区分重载**，含模板/非模板同名实例），勿只写子串 `NAME`（见 §「精确匹配的默认逻辑」）。
   - **禁止在 `--kernel-id` 中使用 `correlation_id` 段**；第 3 段名过滤规则同 **`-k`**；精确匹配时给出合适正则（如 `\bname\b`）。
3. **profile 采样规则**：默认不设 `-s`/`-c`/`--kill`；用户显式要求时再设。叠放顺序：**段 → 名 → 采样**；`per-gpu` / `global` / 多进程见上文采样节。
4. **profile 作用范围**：
   - **工具默认**：不加 **`--profile-child-processes`** → 仅当前进程；**`--devices` 未设** → 全部 device；**`--filter-mode` 未设** → `global`；不设 **`--target-processes-filter`** → 不按进程名筛（主进程始终可被 profile，除非被名过滤排除）。
   - **本 skill 约定（命令生成）**：**`[cuda-app]`** live profile 且用户未明确拒绝时，**默认加入 `--profile-child-processes`** 并**一句话说明**（防漏采子进程 kernel；确认单进程或要缩范围可删）。**例外**：用户明确**仅主进程按名筛**（只写 **tpf**、不加 **pcp**）时，**不要**默认加 **pcp**。用户明确只要当前进程、不要子进程 profile 时亦不加 **pcp**。
   - **`-i *.ixkn-rep`**：勿默认追加 **`--profile-child-processes`**（报告已固化）。
   - **`--target-processes-filter`**（命令生成）：见 §4——要按名筛**含子进程**时 **tpf 默认配 pcp**；用户**只要主进程按名筛**时 **仅 tpf、不加 pcp**。
   - 用户有明确多卡/多进程需求时按用户描述设置；**`--filter-mode` / `--devices` 可与 `--profile-child-processes` 等同用**——多 CPU 进程时**共用同一套** devices / filter-mode / base profile，**不支持**每进程各写不同取值。其余作用范围 option 保持工具默认即可。
5. 除上文**明确限制**外，其余 option 可与 **base profile** 各层**正交叠加**。限制包括：**命令生成时**勿同写 `-k` 与 `--kernel-id`；**`--kill` 无 `-c` 无意义**；**`--kernel-id` 禁止 `correlation_id` 段**。
