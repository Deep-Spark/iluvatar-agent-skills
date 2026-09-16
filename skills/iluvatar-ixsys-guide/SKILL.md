---
name: iluvatar-ixsys-guide
description: 根据用户对 ixsys 的需求生成可复制命令与步骤，适用于 API 追踪、系统级性能分析和常见参数组合（如 --trace、--deferred_trace、--limited_trace、--profile-from-start）。当需求是 CUDA kernel 级深度分析或纯卡状态监控时应路由到 ixkn 或 ixsmi。
---

# ixsys 命令生成

`ixsys` 用于系统级性能分析，可覆盖 CUDA API、NVTX、cuDNN、OSRT、raw_syscalls、video、memory、power 等场景。命令骨架固定：

```bash
ixsys [OPTIONS] [cuda-app] [app-args]
```

- `[OPTIONS]`：`ixsys` 选项集合
- `[cuda-app]`：目标可执行文件
- `[app-args]`：传给目标程序的参数

## 版本适用声明

- 当前语义基线：**ixToolkit v5.0.0**。
- 若用户环境可能是 nightly/internal 或更高版本，先提示可能存在行为差异，再以 `ixsys -h` 与官方文档为准生成命令。

## 何时使用

### 适合使用

- 用户需要 `ixsys` 命令生成、参数解释、组合约束判断。
- 常见关键词：`ixsys`、`trace`、`api`、`callstack`、`performance`、`GPU profile`、`CPU性能`、`性能瓶颈`。

### 不适合使用

- CUDA kernel 级深度分析：路由到 `ixkn`
- 仅卡状态/温度/功耗/显存/进程监控：路由到 `ixsmi`

### ixsys 与 ixkn

| 工具 | 范围 | 开销 | 典型用途 |
|------|------|------|----------|
| `ixsys` | 系统级 | 约 5-10% | 找出值得优化的 kernel 或阶段 |
| `ixkn` | kernel 级 | 慢 10-100 倍 | 解释某个 kernel 为何慢 |

建议流程：先用 `ixsys` 定位热点，再用 `ixkn` 深挖目标 kernel。

## 环境限制

| 项 | 条件 | 说明 |
|----|------|------|
| ixToolkit | v5.0.0 | 默认语义基线 |
| `ixsys` 二进制 | 与 ixToolkit 匹配 | — |
| Iluvatar GPU | BI150 / MR100 / BI300 等 | 以发布说明为准 |
| svGPU | 可用但有限制 | 部分功能（如 power/temperature）受限 |

## 规则总则

1. **先核实来源再生成命令**：每个选项语义需可追溯到本文件、`references/`、`ixsys -h` 或官方文档。
2. **禁止虚构选项**：来源无法核实时，明确说明无法生成，并引导用户用 `ixsys -h` 校验。
3. **一条命令一种输出形态**：避免把查询、print-only、system-wide 固定句式和常规采集混在同一条命令里。
4. **reference 示例是最短演示**：命令生成时仍需遵循本文件步骤约束（例如默认显式写 `--trace cuda,nvtx`）。

## 工作流程

### 先选路径

- **知识查询**：仅解释概念/参数，不必机械执行步骤 1-4。
- **命令生成**：按步骤 1-4 选择选项并给完整命令。
- **命令生成 + 执行**：仅在用户明确要求执行时，进入“可选：执行前自检/实际运行”。

### 统一前提（命令生成路径）

- 查询类命令：步骤 1 直接结束，不进入步骤 2-4。
- 无目标应用的 `system-wide` 固定句式：步骤 1 直接结束，不进入步骤 2-4。
- 仅终端打印且无 `-o`：步骤 1 直接结束，不进入步骤 2-4。
- 只有“有目标应用 + 保存输出（`-o`）”时，步骤 2-4 才生效。

### 异常处理

- 任一步骤发现约束冲突：说明原因并终止后续步骤，不继续拼接非法命令。

## 步骤 1：采集路径判断

目的：先判断是否是查询类命令，再决定是否进入 print-only、system-wide 或常规采集路径。

### 查询类命令

| 选项 | 说明 |
|------|------|
| `-h, --help` | 打印 `ixsys` 帮助信息。代码要求 **`argc == 2`** |
| `-v, --version` | 打印 `ixsys` 版本信息。代码要求 **`argc == 2`** |
| `--sessions-list` | 查询当前运行中的 `ixsys` 会话。代码要求 **`argc == 2`** |
| `--gpu-metrics-device help` | 查询 GPU metrics 可用设备。代码要求 **`argc == 3`** |
| `--gpu-metrics-set help` | 查询 GPU metrics 可用集合。代码要求 **`argc == 3`** |

### 输出模型

- **查询类（直接结束）**：
  - `ixsys --help`
  - `ixsys --version`
  - `ixsys --sessions-list`
  - `ixsys --gpu-metrics-device help`
  - `ixsys --gpu-metrics-set help`
- **仅终端打印（无 `-o`）**：必须有 `[cuda-app] [app-args]`，且受严格约束：
  - `--print-api-summary` / `--print-api-trace` 依赖 `--trace cuda_api` 或 `--trace cuda`
  - `--print-gpu-summary` / `--print-gpu-trace` 依赖 `--trace cuda_device` 或 `--trace cuda`
  - 白名单仅允许：
    - `--profile-from-start`
    - `--target-processes-filter`
    - `--demangling`
    - `--print-kernel-args`
    - `--deferred_trace`
    - `--limited_trace`
- **无目标应用的 system-wide**：命令句式固定：
  - `ixsys -o <file> --trace cuda --cuda-trace-scope system-wide [-d <seconds>] [-l <seconds>]`
- **常规采集**：默认包含 `-o <file>` 且提供目标应用：
  - `ixsys -o <file> [OPTIONS] [cuda-app] [app-args]`

详见 `references/cli-query.md`、`references/cli-display.md`、`references/cli-deviceAndScope.md`、`references/cli-output.md`。

## 步骤 2：确定追踪项与采集指标

目的：确定 trace、nccl-trace、callstack、GPU/NIC/Python 指标与同步采集参数。

| 选项 | 说明 |
|------|------|
| `-t, --trace <tracer>[,...]` | 选择 trace 类型 |
| `--nccl-trace TEXT` | 选择 NCCL trace 子类型 |
| `-c, --callstack` | C/C++ callstack 采样 |
| `--gpu-metrics-device/--gpu-metrics-set/--gpu-metrics-frequency` | GPU metrics 配置 |
| `--nic-metrics {true,false}` | NIC/HCA 指标采集 |
| `--python-sampling/--python-sampling-frequency` | Python 回溯采样 |
| `--event-sync {true,false}` | 跨流同步信息采集 |

### 本 skill 约定（步骤 2）

- 常规默认 trace：若用户未指定，显式写 `--trace cuda,nvtx`。
- `--trace`、`--nccl-trace` 多值只能逗号分隔，不能带空格。
- `--trace` 依赖约束：
  - `raw_syscalls`、`proc_kthread` 必须同写 `osrt`
  - `power`、`temperature` 必须同写至少一个 CUDA 族项
- 父子 trace 归并：如 `cuda,cuda_api -> cuda`、`cuda_device,cuda_kernel -> cuda_device`。
- 父子 nccl-trace 归并：如 `api,api-group -> api`、`coll,p2p,kernel-launch -> rt`。
- `--kernel`、`--frequency` 必须与 `-c, --callstack` 同用。
- `--callstack` 与 `--python-sampling` 需要 root/sudo 权限。
- 只有 `--gpu-metrics-device` 为有效设备值（非 `help`）时，`--gpu-metrics-set` 与 `--gpu-metrics-frequency` 才生效。
- `--event-sync true` 依赖 `--trace cuda_device` 或 `--trace cuda`。

### 执行顺序（步骤 2）

1. 确定 `--trace`（未指定则 `cuda,nvtx`）
2. 确定 `--nccl-trace`
3. 确定 `-c, --callstack`
4. 确定 `--gpu-metrics-*`
5. 确定 `--nic-metrics`
6. 确定 `--python-sampling*`
7. 确定 `--event-sync`
8. 做约束检查（依赖、归并、权限）

详见 `references/cli-trace.md`、`references/cli-metricsProfiling.md`。

## 步骤 3：确定时间/过滤/设备/作用域

目的：在不改变主采集目标的前提下，控制时间窗口、数据规模、设备与进程范围。

| 选项 | 说明 |
|------|------|
| `-d, --deferred_trace <seconds>` | 延迟采集开始时间 |
| `-l, --limited_trace <seconds>` | 限制采集持续时长 |
| `-f, --filter_ftrace_syscalls <microseconds>` | 过滤短时 syscall，降低 trace 体量 |
| `--devices TEXT` | 指定设备范围（默认 `all`） |
| `--cuda-trace-scope {process-tree,system-wide}` | 选择进程树或 system-wide |
| `--target-processes-filter TEXT` | 以进程名表达式过滤采集对象 |
| `--profile-from-start {on,off}` | 是否从程序启动即采集 CUDA 数据 |

### 本 skill 约定（步骤 3）

- `-f` 语义保真：
  - 未说明时可不显式写 `-f`（按默认 `1000us`）
  - 用户要求不过滤时写 `-f 0`
  - 用户给阈值时校验范围 `[0,1000000]`
- `--devices` 与 `--gpu-metrics-device` 可同时使用，作用域互不干涉。
- `--target-processes-filter` 仅在用户明确要求时添加。
- `--profile-from-start off` 必须 trace `cuda` 或其子集。

### 执行要点（步骤 3）

1. 用户未提过滤需求时，不主动增加过滤参数。
2. trace 体量风险较大时，按需增加 `-f` 控量。
3. 一旦添加过滤项，在回答里补一句“为何这样设阈值/对象”。
4. 检查 `--profile-from-start off` 与 trace 约束是否匹配。

### 冲突处理

- 用户要求“全量抓取”时，不应附加缩小范围的过滤参数。
- 用户同时要求“只看某进程”与“system-wide 全量”时，需二选一并显式说明。

详见 `references/cli-timeAndFilter.md`、`references/cli-deviceAndScope.md`。

## 步骤 4：补充展示选项与输出文件

目的：补充可读性与可追溯性，不改变步骤 1-3 已确定的主采集路径。

| 选项 | 说明 |
|------|------|
| `--print-api-summary` / `--print-api-trace` | 终端打印 API summary/trace（依赖 `cuda_api`/`cuda`） |
| `--print-gpu-summary` / `--print-gpu-trace` | 终端打印 GPU summary/trace（依赖 `cuda_device`/`cuda`） |
| `--demangling {on,off}` | 控制函数名 demangling（默认 `on`） |
| `--print-kernel-args` | 打印 kernel 参数 |
| `-o, --output <file>` | 保存 trace/metrics 数据文件（覆盖同名文件） |

### 本 skill 约定（步骤 4）

1. `--print-*` 四项终端打印选项可组合使用。
2. 缺少依赖 trace 时，CLI 会 warning 并忽略不满足条件的 print 项。
3. `--demangling off` 且开启 `--print-kernel-args` 时，命令可运行但 `--print-kernel-args` 实际失效，需要在回答中提示。
4. `-o` 保存的是 trace/metrics 数据，不保存终端打印文本。

### 推荐策略

1. 用户只说“看结果”时，优先给 `-o` + 按需 `--print-*`。
2. 用户明确“仅终端查看”时，才走 print-only 且严格白名单。
3. 需要 UI 展示或复盘时，优先保留 `-o`。
4. 用户明确提到 CPU 侧系统调用/线程调度时，再考虑 `osrt/raw_syscalls/proc_kthread`。
5. 用户明确提到 backtrace 时，再考虑 `-c` 或 `--python-sampling`。

详见 `references/cli-display.md`、`references/cli-output.md`、`references/cli-metricsProfiling.md`。

## 可选：执行前自检（仅在将实际运行 `ixsys` 时）

```bash
ixsys -v
ixsys -h
```

- 知识查询或仅命令生成时，不需要执行自检。
- 若自检失败（命令不存在、版本异常等），不要执行实际采集；只输出可复制命令并提示先修复环境。

## 可选：实际运行命令

仅在以下条件同时满足时执行：

1. 用户明确要求执行
2. 参数齐全且组合合法
3. 若已执行自检，则自检通过

## 推荐回答模板

当用户提出目标时，输出应包含：

1. 是否在 `ixsys` 适用范围内（若不适用，路由到 `ixkn`/`ixsmi`）
2. 步骤 1-4 中关键选项为何这样选（1-2 句）
3. 仅在用户要求且环境就绪时，再执行自检与实际运行

示例：

- 用户：“对 vectorAdd 抓 cuda api 和 osrt”
  - `ixsys -o trace --trace cuda_api,osrt ./vectorAdd`
- 用户：“只看 vectorAdd 开始后 20-40s 的 trace”
  - `ixsys -o trace -d 20 -l 20 ./vectorAdd`（默认 trace 为 `cuda,nvtx`）
- 用户：“直接看 vectorAdd 的 API summary 和 trace”
  - `ixsys --print-api-summary --print-api-trace ./vectorAdd`

## 查找更多信息

### 第一层：本文件（`SKILL.md`）

先按步骤与约束判断命令路径。

### 第二层：`references/`

- `references/cli-query.md`：步骤 1（查询类）
- `references/cli-display.md`：步骤 1/4（终端打印与展示约束）
- `references/cli-trace.md`：步骤 2（`--trace` 与 `--nccl-trace`）
- `references/cli-metricsProfiling.md`：步骤 2（`--callstack`、`--gpu-metrics-*`、`--nic-metrics`、`--python-sampling`、`--event-sync`）
- `references/cli-timeAndFilter.md`：步骤 3（`-d`、`-l`、`-f`）
- `references/cli-deviceAndScope.md`：步骤 3（`--devices`、`--cuda-trace-scope`、`--target-processes-filter`、`--profile-from-start`）
- `references/cli-output.md`：步骤 4（`--output`）
