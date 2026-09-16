# AGENTS.md

本文档供在本仓库中工作的 AI Agent 使用，说明仓库定位、用例组织、构建约束和文档规则。

## 仓库定位

本仓库包含多个面向 Iluvatar ivcore11 GPU（Corex SDK）的 Agent Skill，可供 Claude Code、
Codex、Cursor 和 Copilot 使用。

| Skill | 定位 | Reference 导航 |
|---|---|---|
| `iluvatar-cuda-base` | 平台兼容性差异：编译器/codegen、SDK API、硬件限制、构建迁移 | `skills/iluvatar-cuda-base/references/`：构建迁移、编译器/codegen、inline asm、硬件、driver、SDK API、IXRTC、CCCL、ixBLAS、ixDNN、NPP/CV-CUDA、ONNX Runtime 和 Python 集成 |
| `iluvatar-ixrt` | TensorRT 到 IxRT 迁移、plugin、INT8、`ixrtexec` | `skills/iluvatar-ixrt/references/`：CMake 迁移、不支持接口、内置 plugin、INT8/QDQ、`ixrtexec`、Hook 和 runtime tools |
| `iluvatar-ixjpeg` | nvjpeg/Ixjpeg 硬件 JPEG 编解码与格式边界 | `skills/iluvatar-ixjpeg/references/`：不支持 JPEG 格式边界 |
| `iluvatar-ixcodec` | ixvid*/libnvcuvid 与 IxEnc*/libnvencode 视频编解码、video pipeline | `skills/iluvatar-ixcodec/references/`：decoder、encoder 和完整 video pipeline |
| `iluvatar-ixsys-guide` | `ixsys` 命令生成：系统级 trace、API 追踪、时间线 | `skills/iluvatar-ixsys-guide/references/`：query、display、trace、metrics、time/filter、device/scope、output |
| `iluvatar-ixkncli-guide` | `ixkn-cli` 命令生成：kernel profile、SOL%、`.ixkn-rep` | `skills/iluvatar-ixkncli-guide/references/`：input/output、filter、section/metrics、page/source、addition |
| `iluvatar-ixobjdump-guide` | `ixobjdump` 命令生成：SASS/IR/ELF 查看与提取 | `skills/iluvatar-ixobjdump-guide/references/`：modes、filter、options |

`iluvatar-cuda-base/scripts/gpu-hang-reset/` 仅提供文档和手工恢复脚本。

新增、删除或重命名用例时，必须同步更新所属 skill 的索引和相关文档。IxRT、IxJPEG 和 IXRTC 是三类不同能力：IxRT 是推理栈，IxJPEG 是图像编解码，IXRTC 是 RTC 编译器并归属 `iluvatar-cuda-base`。`ixsys` / `ixkn-cli` / `ixobjdump` 的 CLI 命令生成分别放
`iluvatar-ixsys-guide`、`iluvatar-ixkncli-guide`、`iluvatar-ixobjdump-guide`，不要并入 `iluvatar-cuda-base`。

## Agent Skills 规范

本节是本仓库新增或修改 skill 时的统一检查入口。规则按来源分为四类：

| 标记 | 含义 |
|---|---|
| **[强制规范]** | Agent Skills specification 明确要求，违反时 skill 不合规 |
| **[官方建议]** | Agent Skills 官方的组织或编写建议，可根据仓库实际情况调整 |
| **[仓库规则]** | 本仓库为多客户端兼容、可维护性和验证完整性制定的附加要求 |
| **[用户要求]** | 仓库维护者明确指定的规则，不冒充 Agent Skills 官方要求 |

官方依据：

- [Agent Skills specification](https://agentskills.io/specification)
- [Optimizing skill descriptions](https://agentskills.io/skill-creation/optimizing-descriptions)
- [Best practices](https://agentskills.io/skill-creation/best-practices)

### 目录和 frontmatter

1. **[强制规范]** 每个 skill 是一个目录，并且至少包含 `SKILL.md`。`SKILL.md` 由 YAML
   frontmatter 和 Markdown 正文组成。
2. **[强制规范]** frontmatter 必须包含 `name` 和 `description`。允许的顶层字段只有
   `name`、`description`、`license`、`compatibility`、`metadata` 和实验性的
   `allowed-tools`；`author`、`version` 等扩展信息应作为字符串写入 `metadata`，不能新增同名顶层字段。
3. **[强制规范]** `name` 长度为 1-64 个字符，只使用小写字母、数字和单个短横线，不能以短横线
   开头或结尾，并且必须与 skill 目录名一致。
4. **[强制规范]** `description` 非空且不超过 1024 个字符。`compatibility` 如使用，不超过
   500 个字符。
5. **[官方建议]** `scripts/`、`references/` 和 `assets/` 分别用于可执行代码、按需读取的参考资料
   和输出所需静态资源；它们不是必需目录，也不限制 skill 增加其他必要文件。

### Description 和触发

1. **[官方建议]** `description` 同时说明 skill 做什么、用户在什么情形下应使用它，并包含能够区分
   相邻 skill 的具体术语。以用户意图为主，不把内部目录、实现步骤或完整能力清单都塞入 description。
2. **[官方建议]** description 保持为几句话或一个短段落。规范只有 1024 字符上限；启动阶段
   “约 100 tokens”指全部 skill 的单项 `name + description` 元数据估算，不是 description 的长度目标。

### 渐进披露和资源

1. **[官方建议]** Agent 激活 skill 后会读取完整 `SKILL.md`。主文件只保留每次使用都需要的核心流程、
   决策边界和导航，详细 API 表、证据、完整输出和长示例放到 `references/` 或 `scripts/`。
2. **[官方建议]** `SKILL.md` 保持在 500 行以内，正文建议少于 5000 tokens。接近限制时按主题拆分，
   不能为了缩短主文件而删除 Agent 作出正确判断所需的信息。
3. **[官方建议]** 每个 reference 聚焦一个主题；`SKILL.md` 不只写“查看 references”，而要说明
   遇到什么任务时读取哪个文件。避免同一结论在 `SKILL.md` 和 reference 中重复展开。
4. **[官方建议]** `scripts/` 中的程序应自包含，或者明确记录依赖，并提供可理解的失败信息和必要的
   边界处理。
5. **[官方建议]** 仓内文件使用从 skill 根目录出发的相对路径，并避免深层、循环或无法发现的引用链。
6. **[仓库规则]** 本仓大量 compatibility case 采用
   `SKILL.md -> references/*.md -> scripts/<case>/README.md/源码`：`SKILL.md` 直接给出按任务选择
   reference 的入口，reference 作为一层 case 索引，case README 保存证据。不要在 case README 后
   再建立新的文档链。该组织是本仓库约定，不是 Agent Skills 强制目录结构。
7. **[仓库规则]** 跨 skill 引用只有在共享内容确实由另一 skill 维护时才保留；路径必须可解析，并在
   当前 skill 中说明何时读取。Agent Skills 没有禁止跨 skill 引用，不能仅以“标准化”为由删除。

### 验证和客户端兼容

1. **[仓库规则]** 提交前还要检查相对链接、`SKILL.md` 行数、用例索引、聚合计数和对应脚本结果；
   这些是本仓库工程验证，不属于 Agent Skills 合规判定。
2. **[仓库规则]** 新增、删除或重命名 skill 时，同步 `install.sh`、`skills-manifest.yaml`、根
   `README.md`；实质修改 description 时同步 manifest 中对应摘要。
3. **[用户要求]** 仓库级 Agent 指令统一保存在根目录 `AGENTS.md`，并提供相对软链接
   `CLAUDE.md -> AGENTS.md`。新增规范只维护在 `AGENTS.md`，不要复制出两份内容。

## 安装与聚合入口

```bash
bash install.sh
```

`install.sh` 将清单中的已发布 skill 软链接到 `~/.claude/skills/` 和 `~/.agents/skills/`。暂不发布的 skill 不要写入 `SKILLS` 数组。
 `build.sh`，输出状态如下：

| 状态 | 含义 |
|---|---|
| `PASS` | 用例按预期通过 |
| `XFAIL` | 已登记的预期失败 |
| `XPASS` | 预期失败意外通过，可能需要确认 SDK 是否已修复并删除条目 |
| `FAIL` | 非预期失败，需要排查 |

## 用例目录

编译型 CMake 用例通常采用以下结构：

```text
<name>/
├── CMakeLists.txt
├── build.sh
├── <name>.cu 或 <name>.cpp
└── README.md
```

- `iluvatar-cuda-base` 的编译型 case 通常使用 CMake；Makefile 专项用例保留 Makefile，因为
  构建系统本身就是验证对象，不得迁成 CMake。
- `iluvatar-ixjpeg` 的编译型 case 使用 CMake。
- `iluvatar-ixrt` 和 `iluvatar-ixcodec` 的编译型范例使用 CMake，但运行入口和素材要求以各自
  README/reference 为准。
- 一个 case 必须自包含，不能引用其他 case 的源码、生成物或 build 目录。

## build.sh 规则

1. 以 `cd "$(dirname "$0")"` 起手，保证可从任意工作目录调用。
2. 启用 CUDA language 的 Corex CMake 入口在配置前统一写：

   ```bash
   export CMAKE_CUDA_ARCHITECTURES=ivcore11
   rm -rf build
   cmake -S . -B build || exit 1
   cmake --build build -j"$(nproc)" || exit 1
   ```

   不再通过 `-DCMAKE_CUDA_ARCHITECTURES=ivcore11` 传值。纯 CXX 工程无需设置该变量。
3. NVIDIA 对照优先使用独立的 `build_nv.sh`；CMake 工程启用 CUDA language 时将
   `CMAKE_CUDA_ARCHITECTURES` 设为 `native`，纯 CXX 工程无需设置。
4. Corex 配置命令不传 `-DCMAKE_CUDA_ARCHITECTURES`、`-DCMAKE_CUDA_COMPILER`、
   `-DCUDA_TOOLKIT_ROOT_DIR` 或 `-DCUDAToolkit_ROOT`。目标架构由前述环境变量设置，编译器和
   toolkit 由 CMake 与当前 Corex 环境自动识别。
5. CMake 用例不得在 `build.sh`、`verify_*.sh` 或 `disassemble.sh` 中直接调用 `clang++`、
   `nvcc`、`gcc` 或 `g++` 编译源码；编译必须由真实 CMake target 完成。
6. 可执行 target 和生成文件统一使用 `.out` 后缀；插件、共享库等产物保留 `.so`。
7. 不为统一格式新增优化等级。默认配置能复现时，不再尝试或保留无关的 `-O0/-O1/-O2/-O3`。
   只有问题本身依赖特定优化等级时才显式设置，并在 case README 中说明。
8. 编译失败必须显式检查并返回非零；不要全局使用 `set -e`，因为预期失败用例需要自行捕获
   编译或运行状态。
9. `bash build.sh` 应一次完成该 case 需要的清理、配置、构建、运行和结果判定。分析脚本只消费
   已有二进制，不负责重新编译。
10. CMake 中优先使用 `find_package`、imported target 和 `target_*` 作用域接口；不要写全局
    `include_directories(/usr/local/corex/include)` 或 `link_directories(/usr/local/corex/lib*)`。

## 新增用例

1. 先确定归属：
   - 编译器 bug、silent-wrong、SDK/API 差异、硬件限制放 `iluvatar-cuda-base`。
   - TensorRT/IxRT、JPEG、视频编解码分别放 `iluvatar-ixrt`、`iluvatar-ixjpeg`、
     `iluvatar-ixcodec`。
   - `ixsys`、`ixkn-cli`、`ixobjdump` 的命令生成分别放 `iluvatar-ixsys-guide`、
     `iluvatar-ixkncli-guide`、`iluvatar-ixobjdump-guide`。
2. 名称必须表达技术现象。compatibility/IxRT/IxJPEG/IxCodec 目录通常使用短横线。
3. 编译型 case 至少提供 `CMakeLists.txt`、`build.sh`、自包含源码和 `README.md`；Makefile 专项
   按既有模式处理。
4. 正样本和负样本应尽量放在同一目录中，但各自入口、预期和输出必须清楚；不能依赖执行顺序
   才得到结论。
5. 新增、删除或重命名后，同步更新所属 `SKILL.md`、对应 reference 索引、根 README 和聚合计数。

## 文档规则

- `SKILL.md` 写可供 Agent 直接使用的结论和导航，不堆放长篇证据。完整命令、输出、反汇编和
  根因分析放在对应 `scripts/<case>/README.md` 与源码中。
- 导航链保持为 `SKILL.md -> references/*.md -> scripts/<case>/README.md/源码`。有独立 case 的
  条目在 reference 中保留一行表格和可点击路径，不重复粘贴 case README；没有脚本的内容可在
  reference 后文展开。
- 每项实测结论必须有与之匹配的代码证据。只有实测通过的能力才声明“支持”；实测失败可以写
  “不支持”。未验证或拿不准的能力在能力矩阵中按“不支持”处理，但不得表述为实测结论，也不要写
  “预期不支持”。
- README 要说明验证的 API、关键参数、预期、实际输出和证据边界。描述不得超出最小用例能够
  证明的范围，也不要加入与根因无关的排除性叙述。
- 相近 case 必须在 README 中说明各自验证对象，避免名称、结论和索引重复。
- 文档不写主机名、容器名和本地绝对路径。引用仓内内容使用相对路径；外部官方或参考项目链接
  有助于迁移时应保留为超链接。
- 不写验证环境中的固定 SDK、clang 或共享库版本号；使用“Corex 当前版本”“当前安装包”等表述，
  避免文档随环境升级立即过时。
- 不给本仓最小复现添加无关的上游样例来源说明；确实用于迁移或 API 对照的外部链接可以保留。
- 删除或移动 case 后，清理所有旧索引和历史措辞，不在当前能力文档中继续提已删除条目。
