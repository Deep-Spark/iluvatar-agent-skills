# 指标范围命令指南

> 本文档概述 `ixkn-cli` 的 `--set` / `--section` 等指标范围 option；权威来源以 `ixkn-cli -h` 与官方手册为准。

前置说明：`set` 是多个 `section` 的集合，`section` 是多个 `metric` 的集合，`metric` 来自底层硬件 performance counter（简称 `pfc`）。

**option 分类**：**`--list-sets`** 与 **`--list-sections`** 用于**查询/列出**当前定义（**无需**附带 **`[cuda-app] [app-args]`** 即可执行）。**`--set TEXT`** 与 **`--section TEXT`** 用于在 **live profile** 时指定**指标范围**。

**前置**：执行 **`--list-sets`** / **`--list-sections`** 通常需要本机 **GPU 与驱动**可用；环境不完整时列表可能失败（以 `ixkn-cli` 实际报错为准）。

## set 级设定

1. **`--list-sets`** 列出当前所有 set。指定一个 set 即对其所含全部 section 做指标分析。`Enabled` 列表示 **`--set TEXT`** 的默认候选。

```bash
ixkn-cli --list-sets
```

输出如下所示：

```
---------- --------------------------------------------------------------------------- ------- -----------------
Identifier Sections                                                                    Enabled Estimated Items  
---------- --------------------------------------------------------------------------- ------- -----------------
default    LaunchStats,SpeedOfLight,Occupancy,Instruction,Memory                       yes     89               
detailed   LaunchStats,SpeedOfLight,ComputeWorkload,Occupancy,SchedulerStats,Instructi no      122              
           on,WarpStateStats,Memory
full       LaunchStats,SpeedOfLight,ComputeWorkload,Occupancy,SchedulerStats,Instructi no      122              
           on,WarpStateStats,Memory
---------- --------------------------------------------------------------------------- ------- -----------------
```

2. **`--set TEXT`** 指定一个 set。可选值包括 `default`、`detailed`、`full`。

```bash
ixkn-cli --set default ./vectorAdd
```

## section 级设定

1. **`--list-sections`** 列出当前所有 section。`Enabled` 列表示 **`--section TEXT`** 的默认候选。

```bash
ixkn-cli --list-sections
```

输出如下所示：

```
--------------------------------- ------------------------------------- ------- -------------------------------------------------
Identifier                        Display Name                          Enabled File Name                       
--------------------------------- ------------------------------------- ------- -------------------------------------------------
ComputeWorkload                   Compute Workload Analysis             no      ...er/ixkn/sections/MRPlus/computeworkload.section
Instruction                       Instruction Statistics                yes     ...plorer/ixkn/sections/MRPlus/instruction.section
LaunchStats                       Launch Statistics                     yes     ...plorer/ixkn/sections/MRPlus/launchstats.section
Memory                            Memory Access                         yes     ...ex/ixplorer/ixkn/sections/MRPlus/memory.section
Occupancy                         Occupancy                             yes     ...ixplorer/ixkn/sections/MRPlus/occupancy.section
SchedulerStats                    Scheduler Statistics                  no      ...rer/ixkn/sections/MRPlus/schedulerstats.section
SpeedOfLight                      GPU Speed Of Light                    yes     ...lorer/ixkn/sections/MRPlus/speedoflight.section
WarpStateStats                    Warp State Statistics                 no      ...rer/ixkn/sections/MRPlus/warpstatestats.section
--------------------------------- ------------------------------------- ------- -------------------------------------------------
```

2. **`--section TEXT`** 指定一个或多个 section 以抓取 kernel 性能数据。使用 **`--section all`** 可抓取全部 section。

```bash
ixkn-cli --section Instruction,LaunchStats ./vectorAdd
```

## `--list-*` 与其它 option 的组合（矩阵）

| 组合 | CLI / 文档约定 | 本 skill 命令生成 |
|------|----------------|-------------------|
| **`--list-sets` + `--list-sections`** | **允许**：一次列出 set 与 section 定义 | **允许**（仅此二 flag，无 `[cuda-app]`） |
| **`--list-*` + `--set` / `--section` / `-k` / `-o` / `[cuda-app]` 等** | CLI **可能不报错**且表现为 list 结果，属**未定义行为**，**不保证**未来版本 | **禁止同写**——列表与 profile/过滤/I/O **无关** |
| **`--set` + `--section`**（均无 list） | **允许**：两者取**并集**（CLI 行为） | **命令生成二选一**：只写 **`--set`** 或 **`--section`**，**勿同写** |

```bash
# 允许：仅列表
ixkn-cli --list-sets --list-sections
```

```bash
# 勿生成：list 与 profile 混用（CLI 可能仍只 list，语义未定义）
ixkn-cli --set default --list-sets --list-sections ./test
```

## 补充描述

1. **CLI 行为**：**`--set TEXT`** 与 **`--section TEXT`** **可同时给出**，指标范围为两者**并集**（set 是 section 的集合）。两者都未显式指定时，等效隐式 **`--set default`**。

2. **本 skill 命令生成**：
   - **list 需求**：只生成 **`--list-sets` / `--list-sections`**（可同写），**不带** profile/I/O/过滤/`[cuda-app]` 等——即**舍弃**其它步骤已拼进的 option，列表单独一条命令。
   - **profile 需求**：**`--set` 与 `--section` 二选一**。用户未明确范围 → **显式 `--set default`**；有明确说明 → **优先 `--section`**，仅用户点名 set 时用 **`--set TEXT`**。

```bash
# CLI 允许（生成命令时勿照抄同写）
ixkn-cli --section WarpStateStats --set default ./test
```

## 指标应用规则

1. **`--list-sets`** / **`--list-sections`**：列表命令见上表；**命令生成**时 list 需求**仅**输出 list option，**勿**与 profile 混写。通常仍需 **GPU/驱动** 环境。
2. **profile 命令生成**：用户未指定范围 → **显式 `--set default`**；有明确目标 → **优先 `--section`**（见上「补充描述」#2）。
3. 需要尽可能全的指标时，使用 **`--section all`**（勿与 `--set` 同写）。
4. 按下文 **Identifier 作用概览** 为用户选 **`--section TEXT`**；仅用户明确要求 **`--set TEXT`** 时用 set（且**勿**与 `--section` 同写）。

**Identifier 作用概览**：

- **SpeedOfLight**：GPU 计算/访存资源的高层总览与效率（如 Flop/TCU、L1/L2/DRAM efficiency、各单元 active cycles 等），用于快速判断瓶颈方向（偏算/偏存）。
- **ComputeWorkload**：计算侧工作负载与子单元利用率分析（如 IPC、SPP busy、Scheduler issue busy、ALU/控制流单元利用率等），用于定位算力侧瓶颈。
- **Memory**：访存侧统计与吞吐（如全局/共享内存吞吐、shared bank conflict、cache hit rate、memory requests 等），用于定位内存带宽/冲突/命中率问题。
- **SchedulerStats**：调度器与 warp 发射/就绪/停顿概览（如 active/eligible/issued/stalled warps、stall reason 汇总），用于判断发射槽利用率与调度相关瓶颈。
- **WarpStateStats**：warp 状态与 stall 原因细分（如 memory dependency、sync、fetch、execution dependency 等占比），用于更细粒度定位 stall 根因。
- **Instruction**：指令统计与组成（如 executed instructions、IPC、single/multi-lane、memory/ALU/control-flow 指令拆分等），用于理解指令结构与热点类型。
- **LaunchStats**：kernel 启动配置与资源用量（如 grid/block、warps/threads、寄存器、dynamic/static shared memory 等），用于核对 launch 形态与资源占用。
- **Occupancy**：占用率与限制因素（如 achieved/theoretical occupancy、active warps、各类资源导致的 block limit 等），用于判断是否被寄存器/共享内存等限制。
