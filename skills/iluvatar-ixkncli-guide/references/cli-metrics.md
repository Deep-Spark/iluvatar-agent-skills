# metrics 相关命令：输出整理（保留原始输出）

> 本文档按 option 分段整理：每段包含 **可复制命令**、**text 原始输出（保持不改动）**，以及**简短说明**。  
> **权威用途**：为 **`-m` 命令生成** 提供可核对的 **metric 官方名** 与样例输出；选型流程见 `cli-section.md`「自然语言匹配与 `cli-metrics.md`」。

## 快速索引：该用哪个？

| 需求 | 命令 | 与 live profile 关系 |
|------|------|----------------------|
| 某 section 需要哪些 metric **名字** | `--list-metrics`（可选 `--section` 限定范围） | **list 专用**：**勿**带 `[cuda-app]`、`-o`、`-k` 等 |
| 设备支持的 metric **及 Description** | `--query-metrics` | **query 专用**：同上；§2 含 **中文解释表** |
| live profile 指定 metric 名 | **`-m` / `--metrics`** + **`--page details`** | **`all`** 或逗号分隔官方名（**`all` 勿与点名混写**） |

**注意**：下文 **`--section all --list-metrics`** 里的 **`--section all`** 仅用于**缩小 list 范围**，**不是** live profile 的 **`--section all`**（后者属 `cli-section.md` 六选一中的 section 路径，默认配 **`--page details`**）。

```bash
# list / query：仅列名或查 Description（可无 --section，或 --section all 限定范围）
ixkn-cli --list-metrics
ixkn-cli --query-metrics
```

## 1) `--list-metrics`：按 section 列出 metrics 名称

### bash 命令

```bash
ixkn-cli --section all --list-metrics
```

### text 输出

```text
======== Notice : ixkn-cli will work in internal mode.
---------- SpeedOfLight 's metrics --------
========WARNING No metrics to display

---------- ComputeWorkload 's metrics --------
========WARNING No metrics to display

---------- Memory 's metrics --------
========WARNING No metrics to display

---------- SchedulerStats 's metrics --------
========WARNING No metrics to display

---------- WarpStateStats 's metrics --------
========WARNING No metrics to display

---------- Instruction 's metrics --------
========WARNING No metrics to display

---------- LaunchStats 's metrics --------
========WARNING No metrics to display

---------- Occupancy 's metrics --------
========WARNING No metrics to display

```

### 说明

- 样例输出按 section 展示 metrics 列表；当前版本此命令输出显示 `No metrics to display`——本质上是因为当前 section 直接依赖 events 进行计算，没有直接依赖 metrics；继续保留此项 option 的原因是后续可能有 section 指标直接依赖 metrics；可使用 `--query-metrics` 查看 metrics 信息。

## 2) `--query-metrics`：查询设备支持的 metrics（含 description）

### bash 命令

```bash
ixkn-cli --query-metrics
```

### text 输出

```text
======== Notice : ixkn-cli will work in internal mode.
Available Metrics:
                                      Name   Description
Device 0 (Iluvatar MR-V100):
                             time_duration:  The kernel latency (nanosecond), measured from the GPU timestamps that bound the kernel execution.
                    ns_scheduler_unit_util:  The utilization level of the multiprocessor function units that schedules warps to issue instructions.
           control_flow_function_unit_util:  The utilization level of the multiprocessor function units that execute control flow instructions.
                    alu_function_unit_util:  The utilization level of the multiprocessor function units that execute logic& arithmetic instructions
             load_store_function_unit_util:  The utilization level of the multiprocessor function units that execute load/store instructions
             active_warps_per_active_cycle:  Average number of warps that are in active, max to 8 active warps per scheduler per active cycle. A warp is active from the time it is scheduled on a multiprocessor until it completes the last instruction.
         not_active_warps_per_active_cycle:  Average number of warps that are NOT active, max to 8 NOT active warps per scheduler per active cycle. Active warps + Not active warps = 8 per cycle
           selected_warps_per_active_cycle:  Average number of warps that are selected to issue, max to 4 selected warps per scheduler per active cycle. Each scheduler will select the next warp to issue an instruction from the pool of eligible warps.
           eligible_warps_per_active_cycle:  Average number of warps that are eligible to issue, max to 8 eligible warps per scheduler per active cycle. An active warp is considered eligible if it is able to issue the next instruction.
       not_selected_warps_per_active_cycle:  Average number of warps that are NOT selected to issue, max to 7 NOT selected warps per scheduler per cycle.
            stalled_warps_per_active_cycle:  Average number of warps that are stalled for any kinds of stall reasons, max to 8 stalled warps per scheduler per cycle.
                 selected_warps_percentage:  The percentage of the selected warps in the pool of active warps.
             not_selected_warps_percentage:  The percentage of the NOT selected warps in the pool of active warps.
                  stalled_warps_percentage:  The percentage of the stalled warps in the pool of active warps.
          issue_stall_reason_pipeline_busy:  The percentage of stalled warps in the pool of active warps because a compute resources required by the instruction are not yet available.
        issue_stall_reason_memory_throttle:  The percentage of stalled warps in the pool of active warps because a large number of pending memory operations prevent further forward progress.
      issue_stall_reason_memory_dependency:  The percentage of stalled warps in the pool of active warps because a load/store cannot be made because the required resources are not available or are fully utilized, or too many requests of a given type are outstanding.
        issue_stall_reason_synchronization:  The percentage of stalled warps in the pool of active warps because the warp is blocked at a _syncthreads() call.
      issue_stall_reason_instruction_fetch:  The percentage of stalled warps in the pool of active warps because the next kernel instruction has not yet been fetched.
   issue_stall_reason_execution_dependency:  The percentage of stalled warps in the pool of active warps because an input or output required by the instruction is not yet available (RAW[read after write], WAR, WAW data dependency).
                  issue_stall_reason_other:  The percentage of stalled warps in the pool of active warps because of other reasons.
                   multiprocessor_activity:  The percentage of time at least one warp is active on a SPP.
                            warps_launched:  Total number of warps launched.
                 control_flow_instructions:  Number of executed control flow instructions.
                      integer_instructions:  Number of integer instructions, including SL integer and ML integer instructions.
             float_point_instructions_half:  Number of half-precision instructions.
           float_point_instructions_single:  Number of single-precision instructions.
                   load_store_instructions:  Number of executed load/store instructions, including instructions of SL-LSA, ML-LSA and ML-SLB.
                                       ipc:  Instructions executed per cycle.
          warp_execution_efficiency_ml_alu:  Ratio of the average active threads per warp to the maximum number of threads per warp supported in ML ALU execution.
          warp_execution_efficiency_ml_mem:  Ratio of the average active threads per warp to the maximum number of threads per warp supported in ML Mem execution.
                     instructions_per_warp:  Average number of instructions executed by each warp.
  float_point_operation_half_precision_mul:  Number of half-precision floating-point multiply operations.
  float_point_operation_half_precision_add:  Number of half-precision floating-point add operations.
  float_point_operation_half_precision_fma:  Number of half-precision floating-point FMA operations.
float_point_operation_single_precision_mul:  Number of single-precision floating-point multiply operations.
float_point_operation_single_precision_add:  Number of single-precision floating-point add operations.
float_point_operation_single_precision_fma:  Number of single-precision floating-point FMA operations.
                               matrix_inst:  Number of Matrix instruction.
                                  sfu_inst:  Number of SFU instruction.
                             multiply_inst:  Number of 4-pass multiply instruction.
                  shared_load_transactions:  Number of shared memory load transactions.
                 shared_store_transactions:  Number of shared memory store transactions.
            shared_atomic_rdc_transactions:  Number of shared memory atomic and reduction transactions.
      shared_load_transactions_per_request:  Average number of shared memory load transactions performed for each shared memory load.
     shared_store_transactions_per_request:  Average number of shared memory store transactions performed for each shared memory load.
shared_atomic_rdc_transactions_per_request:  Average number of shared memory atomic and reduction transactions performed for each shared memory atomic and reduction.
                    shared_load_throughput:  Shared memory load throughput.
                   shared_store_throughput:  Shared memory store throughput.
                         mem_load_requests:  Total number of memory(global + local) load requests from Multiprocessor.
                        mem_store_requests:  Total number of memory(global + local) store requests from Multiprocessor.
                       mem_atomic_requests:  Total number of memory(global + local) atomic requests from Multiprocessor.
                     mem_load_transactions:  Number of memory(global + local) load transactions.
                    mem_store_transactions:  Number of memory(global + local) store transactions.
                   mem_atomic_transactions:  Number of memory(global + local) atomic transactions.
         mem_load_transactions_per_request:  Average number of memory(global + local) load transactions performed for each memory load.
        mem_store_transactions_per_request:  Average number of memory(global + local) store transactions performed for each memory store.
       mem_atomic_transactions_per_request:  Average number of memory(global + local) atomic transactions performed for each memory atomic.
                       mem_load_throughput:  Memory(global + local) load throughput.
                      mem_store_throughput:  Memory(global + local) store throughput.
                          l1vk_ld_hit_rate:  L1VK cache hit rate on memory load access(ca policy).
                          l1vk_st_hit_rate:  L1VK cache hit rate on memory store access(ca policy).
                          l1vk_utilization:  L1VK cache utilization level.
                              llc_hit_rate:  Cache hit rate of requests from Fabric to LLC.
                               llc_mem_req:  Requests from LLC to Device Memory.
               pcie_total_data_transmitted:  Total data bytes transmitted through PCIe.
                  pcie_total_data_received:  Total data bytes received through PCIe.

```

### 中文解释表（`--query-metrics`）

| Name | 中文说明 |
|------|----------|
| `time_duration` | kernel 延迟（nanosecond），由包围 kernel 执行的 GPU 时间戳测得 |
| `ns_scheduler_unit_util` | 负责调度 warp 发射指令的多处理器功能单元利用率 |
| `control_flow_function_unit_util` | 执行控制流指令的功能单元利用率 |
| `alu_function_unit_util` | 执行逻辑与算术指令的功能单元利用率 |
| `load_store_function_unit_util` | 执行 load/store 指令的功能单元利用率 |
| `active_warps_per_active_cycle` | active warp 平均数（每 scheduler 每 active cycle 最多 8） |
| `not_active_warps_per_active_cycle` | 非 active warp 平均数（active+not active=8 per cycle） |
| `selected_warps_per_active_cycle` | 被选中发射的 warp 平均数（每 scheduler 每 active cycle 最多 4） |
| `eligible_warps_per_active_cycle` | eligible warp 平均数（每 scheduler 最多 8） |
| `not_selected_warps_per_active_cycle` | 未被选中发射的 warp 平均数（每 scheduler 每 cycle 最多 7） |
| `stalled_warps_per_active_cycle` | 因各类 stall 原因的 warp 平均数（每 scheduler 最多 8） |
| `selected_warps_percentage` | active warp 池中被选中 warp 的百分比 |
| `not_selected_warps_percentage` | active warp 池中未被选中 warp 的百分比 |
| `stalled_warps_percentage` | active warp 池中 stall warp 的百分比 |
| `issue_stall_reason_pipeline_busy` | 因所需计算资源尚未就绪导致 stall 的 warp 占比 |
| `issue_stall_reason_memory_throttle` | 因挂起内存操作过多阻碍前进导致 stall 的 warp 占比 |
| `issue_stall_reason_memory_dependency` | 因 load/store 所需资源不可用或某类请求过多导致 stall 的 warp 占比 |
| `issue_stall_reason_synchronization` | 因在 _syncthreads() 阻塞导致 stall 的 warp 占比 |
| `issue_stall_reason_instruction_fetch` | 因下一条指令尚未取到导致 stall 的 warp 占比 |
| `issue_stall_reason_execution_dependency` | 因 RAW/WAR/WAW 等执行依赖导致 stall 的 warp 占比 |
| `issue_stall_reason_other` | 因其它原因 stall 的 warp 占比 |
| `multiprocessor_activity` | 至少一个 warp 在 SPP 上 active 的时间占比 |
| `warps_launched` | 发射 warp 总数 |
| `control_flow_instructions` | 已执行控制流指令数 |
| `integer_instructions` | 整数指令数（含 SL/ML integer） |
| `float_point_instructions_half` | 半精度浮点指令数 |
| `float_point_instructions_single` | 单精度浮点指令数 |
| `load_store_instructions` | 已执行 load/store 指令数（SL-LSA、ML-LSA、ML-SLB） |
| `ipc` | 每 cycle 执行指令数（IPC） |
| `warp_execution_efficiency_ml_alu` | ML ALU 执行时 warp 平均 active 线程数与最大线程数之比 |
| `warp_execution_efficiency_ml_mem` | ML Mem 执行时 warp 平均 active 线程数与最大线程数之比 |
| `instructions_per_warp` | 每个 warp 平均执行指令数 |
| `float_point_operation_half_precision_mul` | 半精度浮点 mul 操作数 |
| `float_point_operation_half_precision_add` | 半精度浮点 add 操作数 |
| `float_point_operation_half_precision_fma` | 半精度浮点 FMA 操作数 |
| `matrix_inst` | Matrix 指令数 |
| `sfu_inst` | SFU 指令数 |
| `multiply_inst` | 4-pass multiply 指令数 |
| `shared_load_transactions` | 共享内存 load 事务数 |
| `shared_store_transactions` | 共享内存 store 事务数 |
| `shared_atomic_rdc_transactions` | 共享内存 atomic 与 reduction 事务数 |
| `shared_load_transactions_per_request` | 每次 shared load 平均事务数 |
| `shared_store_transactions_per_request` | 每次 shared store 平均事务数 |
| `shared_load_throughput` | 共享内存 load 吞吐 |
| `shared_store_throughput` | 共享内存 store 吞吐 |
| `mem_load_requests` | 多处理器发起的 global+local load 请求总数 |
| `mem_store_requests` | 多处理器发起的 global+local store 请求总数 |
| `mem_atomic_requests` | 多处理器发起的 global+local atomic 请求总数 |
| `mem_load_transactions` | global+local load 事务数 |
| `mem_store_transactions` | global+local store 事务数 |
| `mem_atomic_transactions` | global+local atomic 事务数 |
| `mem_load_transactions_per_request` | 每次 memory load 平均事务数 |
| `mem_store_transactions_per_request` | 每次 memory store 平均事务数 |
| `mem_atomic_transactions_per_request` | 每次 memory atomic 平均事务数 |
| `mem_load_throughput` | global+local load 吞吐 |
| `mem_store_throughput` | global+local store 吞吐 |
| `l1vk_ld_hit_rate` | memory load(ca policy) 的 L1VK hit rate |
| `l1vk_st_hit_rate` | memory store(ca policy) 的 L1VK hit rate |
| `l1vk_utilization` | L1VK cache 利用率 |
| `llc_hit_rate` | Fabric 到 LLC 请求的 cache hit rate |
| `llc_mem_req` | LLC 到 Device Memory 的请求数 |
| `pcie_total_data_transmitted` | 经 PCIe 发送数据总字节数 |
| `pcie_total_data_received` | 经 PCIe 接收数据总字节数 |

### 说明

- 查询设备支持的 metrics（Name + Description）；上表 **text** 为 CLI 原文，**中文解释表** 便于 `-m` 选型。
