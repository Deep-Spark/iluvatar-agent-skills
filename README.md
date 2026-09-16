# Iluvatar Agent Skills

---

## 简要介绍

Iluvatar Agent Skills 是面向天数智芯软件栈的 Skills 参考实现仓库，基于开源 Agent 能力构建，用于：

- 为开发者与用户提供可参考的 Skills 实现
- 推动天数智芯 Skills 的开放共享与持续演进

Agent Skills 是以本地目录形式组织的可复用能力包，用于向 Agent 注入领域知识、工作流与最佳实践，使其从通用型变为专家型。与一次性 Prompt 不同，Skills 可按需加载，无需在多轮对话中重复说明。

- 专精能力：为特定领域定制稳定工作流。
- 一次创建，多平台复用（CodeBuddy、TRAE、Cursor、OpenClaw 等）。
- 可组合：多个 Skills 组合构建复杂流程。

## Skills 总览

| Skill                                                          |                                   适用                                    | 状态  |
| -------------------------------------------------------------- | :-----------------------------------------------------------------------: | :---: |
| [`iluvatar-cuda-base`](skills/iluvatar-cuda-base/)             |                平台兼容性差异 / 编译器 bug / SDK API 差异                 |   🚧   |
| [`iluvatar-ixrt`](skills/iluvatar-ixrt/)                       |   TensorRT 工程迁 corex / nvinfer→ixrt / plugin V3→V2 / INT8 / ixrtexec   |   ✅   |
| [`iluvatar-ixjpeg`](skills/iluvatar-ixjpeg/)                   |            nvjpeg/cujpeg 硬件 JPEG 编解码 / NV12 编 jpg / 色偏            |   ✅   |
| [`iluvatar-ixcodec`](skills/iluvatar-ixcodec/)                 |          nvviddec/nvEncodeAPI 视频硬件编解码 / NVDEC·NVENC 迁移           |   ✅   |
| [`iluvatar-ixsys-guide`](skills/iluvatar-ixsys-guide/)         |         面向 `ixsys` 的命令生成（系统级 trace、API 追踪、时间线）         |   ✅   |
| [`iluvatar-ixkncli-guide`](skills/iluvatar-ixkncli-guide/)     | 面向 `ixkn-cli` 的命令生成（kernel profile、`-i/-o`、过滤/指标/源码选项） |   ✅   |
| [`iluvatar-ixobjdump-guide`](skills/iluvatar-ixobjdump-guide/) |           面向 `ixobjdump` 的命令生成（SASS/IR/ELF 查看与提取）           |   ✅   |

状态：✅ 已发布（可安装）· 🚧 暂不发布（仓内可查阅，不列入默认安装）。

## 快速开始

### 从仓库本地安装

已克隆本仓库时，在根目录执行：

```bash
bash install.sh
```

`install.sh` 将清单中的已发布 skill 软链到 `~/.claude/skills/`（Claude Code）和 `~/.agents/skills/`（Cursor / Codex / Copilot）。暂不发布的 skill 不会安装。没有按单个 skill 或按工具分开安装的参数。

```bash
bash install.sh --dry-run      # 预览，不落盘
bash install.sh --uninstall    # 移除软链
```

### 用 skills CLI 安装

使用默认的 [`skills` CLI](https://github.com/vercel-labs/skills) 安装本仓库的 Skills：

```bash
npx skills add deep-spark/iluvatar-agent-skills
```

CLI 通过 `npx` 运行，会提示选择要安装的 Skill 以及安装目标。无需克隆本仓库，也不必手工复制 skill 目录。安装时请选择状态为 ✅ 的 Skill。

Agent 下次加载 Skills 并遇到相关任务时即可使用。例如，让 Agent「把 TensorRT 工程迁到 Corex / IxRT」，对应 Skill 会按平台差异与迁移流程引导。

### 安装单个 Skill

已确定 Skill 名称，希望跳过交互提示时使用：

```bash
npx skills add deep-spark/iluvatar-agent-skills --skill iluvatar-ixrt --yes
```

将 `iluvatar-ixrt` 替换为 [Skills 总览](#skills-总览) 中状态为 ✅ 的 Skill 名称。

### 为特定 Agent 安装 Skills

使用 `--agent` 指定目标 AI 编程 Agent。本仓库面向常见客户端，后续会随规范扩展。完整客户端列表见 [`skills` CLI 支持的 Agent 表](https://github.com/vercel-labs/skills#supported-agents)。

#### TRAE

```bash
npx skills add deep-spark/iluvatar-agent-skills --skill iluvatar-ixrt --agent trae
```

#### Cursor

```bash
npx skills add deep-spark/iluvatar-agent-skills --skill iluvatar-ixrt --agent cursor
```

#### OpenClaw

```bash
npx skills add deep-spark/iluvatar-agent-skills --skill iluvatar-ixrt --agent openclaw
```

多次传入 `--agent`，可将同一 Skill 安装到多个 Agent：

```bash
npx skills add deep-spark/iluvatar-agent-skills \
  --skill iluvatar-ixrt \
  --agent trae \
  --agent cursor \
  --agent openclaw
```

### 如何更新 Skills

目录会持续新增 Skill，已有条目也可能修订、重命名或合并。刷新已安装内容：

```bash
npx skills update
```

交互运行时，CLI 还会标出上游已删除或已合并的 Skill（例如多个 Skill 合并为一个），并询问是否清理本地过期副本。用 `npx skills list` 查看已安装项，用 `npx skills check` 先预览哪些已过期。

### 浏览目录

安装前先查看本仓库有哪些 Skill：

```bash
npx skills add deep-spark/iluvatar-agent-skills --list
```

## 目录结构

```shell
skills/<your-skill-name>/
├── SKILL.md              # ✅ 必需：技能主文件
├── README.md             # ✅ 推荐：项目概览说明
├── scripts/              # ✅ 可选：可执行脚本
│   └── your-script.sh
├── references/           # ✅ 可选：参考文档
│   └── guide.md
└── assets/               # ✅ 可选：资源文件
    ├── template.md
    └── checklist.md
```

## SKILL命名规范

- SKILL.md 文件必须严格命名为 SKILL.md（区分大小写），不接受任何变体（如 SKILL.MD、skill.md）。
- 名称应清晰反映 Skill 核心功能，且长度 ≤ 64 字符
- SKILL 文件夹命名必须使用烤串命名法（kebab-case），例如“iluvatar-ixrt”
  - ✅ 正确示例：iluvatar-ixrt
  - ❌ 错误示例：Iluvatar ixRT（含空格）
  - ❌ 错误示例：iluvatar_ixrt（使用下划线）
  - ❌ 错误示例：IluvatarIxRT（使用大写）

## 标准和兼容性

本仓库遵循 [Agent Skills 规范](https://agentskills.io/specification):

- Skills 是可移植的目录，根目录下需有 SKILL.md 文件。
- 元数据使用 YAML frontmatter，并要求包含 name 与 description 字段。
- Skills 采用渐进式披露模型——启动时仅加载轻量元数据，激活时再加载完整指令。
可使用 [`skills-ref`](https://github.com/agentskills/agentskills/tree/main/skills-ref) 参考库校验你的 skill。

## 寻求帮助

**按问题类型选择反馈渠道：**

- **Skill 内容问题**（某个 Skill 有缺陷、能力缺失或内容错误）— 请在本仓库提交 Issue，并注明对应 Skill 名称，见 [Skills 总览](#skills-总览)。
- **仓库与文档问题**（README 错误、安装与分发流程、本仓库文档）— 请在[此处](https://github.com/Deep-Spark/iluvatar-agent-skills/issues/new/choose)提交，可选用 **缺陷报告**、**功能请求** 或 **文档修正**。
- **提问或一般讨论** — 使用 [Discussions](https://github.com/Deep-Spark/iluvatar-agent-skills/discussions)。Issue 跟踪器仅用于缺陷报告、带设计说明的功能提案，以及文档问题。

欢迎加入天数智芯开发者社区，一起探讨 Skills 开发与使用经验、反馈问题与建议、获取最新动态。扫描下方二维码即可加入微信交流群，期待您的参与~

![qr code](docs/images/helper-qr-code.png)

## 使用说明

本仓库开源的 Skills 旨在帮助社区开发者更高效地在天数智芯 GPGPU 上进行开发、调试与迁移。欢迎在许可范围内使用、修改和再分发。

Skills和示例可能随软件栈演进或本地Agent差异而发生行为变化，不保证完整、准确或始终适用于你的环境。平台能力与接口请以天数智芯官方文档和发行包为准。用于生产或关键业务前，请自行验证安全性、兼容性与第三方依赖的授权。

## 许可证

Copyright (c) 2026, Shanghai Iluvatar CoreX Semiconductor Co., Ltd. All Rights Reserved.

本仓库采用双许可证：源代码遵循 Apache-2.0，文档/Skills 遵循 CC-BY-4.0。完整许可证文本分别见 [LICENSE-APACHE](LICENSE-APACHE) 与 [LICENSE-CC-BY-4.0](LICENSE-CC-BY-4.0)。
