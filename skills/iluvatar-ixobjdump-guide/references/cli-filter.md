# 过滤与范围：`--fun` / `--findex` / `--arch` / `--all`

> 权威：`ixobjdump -h`。匹配行为以实测补充。

## 选项说明

| option | 短/别名 | 说明 |
|--------|---------|------|
| `--function TEXT` | `--fun` | 指定要 dump 的设备函数；**逗号分隔多个正则** |
| `--function-index TEXT` | `--findex` | 按序号选函数；**单个正则**或**逗号分隔数字** |
| `--gpu-architecture TEXT` | `--arch` | 只展示指定 GPU 架构；逗号分隔多值 |
| `--all-fatbin` | `--all` | dump 全部 fatbin 段（默认优先可执行 fatbin） |

允许的 `--arch` 值（`-h`）：`'ivcore10','ivcore11','ivcore20','ivcore30','ivcore40'`。

## `--fun` 匹配规则（命令生成）

1. 过滤对象是 **C++ mangled 设备函数名**（如 `_Z4afwdPfS_S_`），不是 demangled 显示名 alone。
2. Pattern 按**正则**解释；实测对全名做匹配时：
   - `_Z4afwdPfS_S_` → 命中
   - `.*afwd.*` → 命中
   - `afwd`、`_Z4afwd`（非完整锚定/未覆盖全名）→ 可能 **not found**
3. **本 skill 约定**：用户给短名 `NAME` 时，默认写 **`--fun '.*NAME.*'`**；已知完整 mangled 名则直接写全名。多个名字：`--fun '.*a.*,.*b.*'` 或精确全名逗号列表。
4. 不确定 mangled 名时，先：

```bash
ixobjdump --ltext <file>
# 或
ixobjdump --symbols <file>
```

## `--findex` 规则

1. 序号对应 fatbin/符号顺序中的设备函数；**从 1 开始**。
2. 实测：`--findex 0` → fatal（非法）；超出范围 → warning not found。
3. 用户说“第 N 个 kernel/函数”时用 `--findex N`，并建议配合 `--sass`（或当前 dump 模式）。
4. 与 `--fun`：**命令生成默认二选一**，避免同写。

## `--arch` 规则

1. 多架构 fatbin 时用于收窄输出。
2. **先确认实际 arch**（`--IR` 头里的 `arch = …`，或文档/编译目标），再写 `--arch`。
3. 对**不存在**的架构做 dump 时，工具可能异常退出（实测：对仅含 MR/ivcore11 的 `afwd` 使用 `--arch ivcore10 --sass` 曾 abort）。命令生成遇到不确定 arch 时，宁可省略 `--arch` 或先 list/dump 头信息。

## `--all`

- 与某一 dump 同用（如 `--all --sass`），扩大段范围。
- 单架构、单可执行 fatbin 时与默认 dump 观感可能相同；仅在用户明确要“全部 fatbin 段”时添加。

## 示例

```bash
# 短名 → 正则
ixobjdump --fun '.*afwd.*' --sass ./afwd

# 精确 mangled
ixobjdump --fun '_Z4afwdPfS_S_' --sass ./afwd

# 第 1 个设备函数
ixobjdump --findex 1 --sass ./afwd

# 指定架构（确认后）
ixobjdump --arch ivcore11 --sass ./afwd
```
