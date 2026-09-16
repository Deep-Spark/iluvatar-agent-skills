# 展示类选项（Display Options）

> 本文档概述 `ixsys` 的展示类选项。权威来源以 `ixsys -h` 与官方手册为准。

## 选项说明

| 选项 | 说明 |
|------|------|
| `--print-api-summary` / `--print-api-trace` | 终端打印 CUDA runtime/driver API 的 summary/trace。 |
| `--print-gpu-summary` / `--print-gpu-trace` | 终端打印 GPU 活动 summary/trace（含 kernel、graph、memcpy、memset）。 |
| `--demangling {on,off}` | 控制函数名 demangling，默认 `on`。 |
| `--print-kernel-args` | 打印 kernel 名及其实参。 |

## 关键约束（与 `SKILL.md` 步骤 4 一致）

1. `--print-api-summary`、`--print-api-trace` 依赖 `--trace cuda_api` 或 `--trace cuda`。
2. `--print-gpu-summary`、`--print-gpu-trace` 依赖 `--trace cuda_device` 或 `--trace cuda`。
3. 上述四个 `--print-*` 选项可任意组合，但都要求提供 `[cuda-app] [app-args]`。
4. 终端打印数据来源于 profiling 原始数据；`--devices` 仅限制 `-o` 文件中的保存范围，不控制 `--print-api/gpu-*` 的终端输出范围。

## 使用规则

1. **终端打印（加 `-o`）**：打印与数据保存同时进行，`-o` 保存的是 trace/metrics 数据，不保存终端打印文本（详见 `./cli-output.md`）。
2. **终端打印（无 `-o`）**：仅终端查看，不保存文件。该模式下仅允许额外出现：
   - `--profile-from-start`
   - `--target-processes-filter`
   - `--demangling`
   - `--print-kernel-args`
   - `--deferred_trace`
   - `--limited_trace`
3. `--demangling off` 与 `--print-kernel-args` 同时设置时，CLI 可运行，但 `--print-kernel-args` 实际失效（无法展示可读实参）。

## 快速示例

```bash
# 仅终端打印 API 结果
ixsys --print-api-summary --print-api-trace ./vectorAdd

# 终端打印 + 保存 trace 文件
ixsys -o trace --print-api-summary --print-gpu-summary ./vectorAdd

# 指定 trace 后打印 GPU summary
ixsys -o trace --print-gpu-summary --trace cuda,osrt ./vectorAdd
```
