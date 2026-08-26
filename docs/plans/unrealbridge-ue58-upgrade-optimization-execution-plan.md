# UnrealBridge UE 5.8.1 升级后优化执行计划

> 文档状态：待执行，阻塞于 ShooterRoyal 完成 UE 5.8.1 迁移
> 创建日期：2026-08-25
> UnrealBridge 工作仓库：`C:\dev\UnrealBridge`
> 集成项目：`C:\dev\ShooterRoyal`
> 当前基线：UnrealBridge `2.0.0`、Protocol `2`、UE `5.6.1`
> 目标引擎：Unreal Engine `5.8.1`

---

## 1. 文档目的与权威范围

本文档定义 ShooterRoyal 升级到 UE 5.8.1 后，UnrealBridge 的兼容、架构接入、可靠性优化和能力扩展顺序。它不是 ShooterRoyal 引擎迁移手册，也不会在 UE 5.8.1 安装完成前触发任何代码或 Content 写入。

执行目标：

1. 在 UE 5.8.1 上完成 UnrealBridge 插件、ShooterRoyal Editor Target 和实际运行链路验证。
2. 接入 UE 5.8 官方 `ModelContextProtocol`、`ToolsetRegistry` 和可用 Toolsets，但不以官方 Experimental MCP 替换 UnrealBridge 的可靠 Job、ChangeSet、鉴权和恢复层。
3. 不只补 Niagara，系统性吸收 Slate、Automation、Blueprint、Material、GameFeature、GAS、AI、PCG、Animation、Sequencer、Physics、Audio、Networking 等可复用能力。
4. 对官方 Toolset 已覆盖的能力优先复用或薄封装；只在官方能力缺少深度、安全语义或 ShooterRoyal 所需高层意图时新增 UnrealBridge 原生实现。
5. 继续维护 UE 5.3 至 UE 5.7 的现有路径，不因 UE 5.8 集成移除 TCP/UDP discovery、HTTP Job endpoint 或旧版本兼容层。

发生冲突时，本文档取代 [`eda-integration-roadmap.md`](./eda-integration-roadmap.md) 中关于 UE 5.8 的执行顺序和架构假设。旧文档仍作为历史调研保留。已经完成的可靠性底座以 [`unrealbridge-optimization-execution-plan.md`](./unrealbridge-optimization-execution-plan.md) 为准。

---

## 2. 当前基线

### 2.1 UnrealBridge

- 插件版本：`2.0.0`
- 协议版本：`2`
- 当前反射 manifest：`34` 个 library、`1179` 个 function
- 当前 manifest 来源：UE `5.6.1-44394996`
- 当前能力：TCP/UDP discovery、loopback HTTP MCP/REST、durable Job、polling Job、幂等、结构化错误、ChangeSet、package ownership、类型化 manifest 和 kwargs-only wrapper
- 已有 BuildPlugin 记录：UE `5.3.2 / 5.4.4 / 5.5.4 / 5.6.1 / 5.7.1 / 5.8.0`
- 未完成验证：UE `5.8.1` Launcher Build、ShooterRoyal UE 5.8.1 Editor Target、官方 ToolsetRegistry 实际 schema
- UE 5.7+ 才启用的现有实现：Material、Chooser、Geometry、Perf 以及部分 Blueprint、DataTable、GAS、Navigation 接口

当前 UnrealBridge 工作区已有 3 个生成文件改动，都是 2026-08-04 重新生成后的 manifest/hash 更新：

- `.claude/skills/unreal-bridge/scripts/bridge_manifest.json`
- `Plugin/UnrealBridge/Content/Python/bridge_manifest_meta.json`
- `Plugin/UnrealBridge/Content/Python/unreal_bridge.py`

执行时必须保留并审计这些改动，不得用旧生成物覆盖。

### 2.2 ShooterRoyal

- 当前 EngineAssociation：`5.6`
- 当前自定义资产：`/ShooterRoyal/**` 共 `325` 个
- 高回归风险资产：`92` 个 Blueprint、`77` 个 WidgetBlueprint、`5` 张地图
- 项目插件模块：`19` 个，全部有源码，无 `.uplugin` 固定 `EngineVersion`
- Lyra 基线偏离：`9` 个 `Source/LyraGame` 文件和 `3` 个 `ShooterCore` 资产
- 当前 Git 状态：`502` 条，其中 `256` tracked、`246` untracked
- Git LFS：`6164` 个跟踪文件，`git lfs fsck` 当前通过

这意味着技术上适合升级，但升级前必须先建立同时覆盖 Git 历史和 LFS 对象的可恢复快照。

---

## 3. 执行前 Gate

任何开发批次开始前必须同时满足：

| Gate | 条件 | 证据 |
|---|---|---|
| G0 | ShooterRoyal 已建立可恢复快照 | 快照分支、tag、远端或离线副本验证通过 |
| G1 | UE 5.8.1 与 UE 5.6.1 并存安装 | 两个 Editor 路径均存在 |
| G2 | ShooterRoyal 已在独立副本或迁移分支完成 5.8.1 转换 | `.uproject` 指向 5.8，旧项目仍可由 5.6.1 打开 |
| G3 | 5.8.1 匹配版本 Lyra 已安装并完成基线对照 | 新 Lyra 可编译，12 个基础偏离已逐项决定保留或淘汰 |
| G4 | ShooterRoyal 5.8.1 C++ Editor Target 先于 UnrealBridge 优化通过 | `LyraEditor Win64 Development` 成功 |
| G5 | Editor 可稳定启动 | PIE=false、无无关 Dirty Package、无 startup crash |
| G6 | UnrealBridge 仓库单独建立开发分支 | 当前生成文件与本文档均有可恢复提交 |

任一 Gate 不满足，停止能力开发，只处理迁移或构建阻塞。

---

## 4. 升级前可恢复快照

### 4.1 为什么不能只用 `git bundle`

ShooterRoyal 使用 Git LFS 跟踪 `.uasset`、`.umap`、`.uproject` 和其他二进制文件。普通 Git commit 和 `git bundle` 保存的是 LFS pointer，不保证包含 `.git/lfs/objects` 中的实际二进制内容。

因此有效快照必须同时具备：

1. 一个提交，包含全部需要保留的 tracked 和 untracked 文件。
2. 对应提交引用的全部 LFS objects。
3. 至少一次独立位置的恢复验证。

不把 `git stash -u` 作为升级快照。Stash 隐蔽、容易被清理，也不独立保存 LFS 对象。

### 4.2 推荐方案：快照分支 + 远端 LFS

在 UE Editor 关闭后运行。命令只用于未来执行，本轮不运行：

```powershell
Set-Location C:\dev\ShooterRoyal

$Stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$SnapshotBranch = "codex/pre-ue58-snapshot-$Stamp"
$SnapshotTag = "pre-ue58-$Stamp"

git switch -c $SnapshotBranch
git add -A
git status --short
git commit -m "chore: snapshot before UE 5.8.1 migration"
git tag -a $SnapshotTag -m "Recoverable snapshot before UE 5.8.1 migration"

git lfs fsck
git lfs push --all origin $SnapshotBranch
git push -u origin $SnapshotBranch
git push origin $SnapshotTag
```

`git add -A` 会把当前 tracked 和未忽略的 untracked 内容纳入提交。`Binaries/`、`Intermediate/`、`DerivedDataCache/`、`Saved/` 和 `.vs/` 仍由 `.gitignore` 排除，不应作为项目源快照上传。

推送后必须做独立 clone 验证：

```powershell
Set-Location C:\dev\ShooterRoyal

$RemoteUrl = git remote get-url origin
$VerifyPath = "C:\dev\migration-verification\ShooterRoyal-$Stamp"

git clone --branch $SnapshotBranch $RemoteUrl $VerifyPath
git -C $VerifyPath lfs pull
git -C $VerifyPath lfs fsck
git -C $VerifyPath status --short
```

通过条件：

- `git lfs fsck` 输出成功。
- 验证 clone 没有缺失 LFS object。
- 验证 clone 的 `git status --short` 为空。
- 随机抽查至少 1 个 `.umap`、5 个 `.uasset`、1 个 `.cpp`、1 个 `.h` 和 1 个未跟踪后纳入提交的文档，文件存在且非 LFS pointer 文本。

### 4.3 离线备份方案：Git bundle + LFS object 副本

当远端不可用或需要第二份本地保险时执行：

```powershell
Set-Location C:\dev\ShooterRoyal

$BackupRoot = "C:\dev\backups\ShooterRoyal-pre-ue58-$Stamp"
New-Item -ItemType Directory -Path $BackupRoot

git bundle create "$BackupRoot\ShooterRoyal.bundle" --all
git bundle verify "$BackupRoot\ShooterRoyal.bundle"

robocopy `
  "C:\dev\ShooterRoyal\.git\lfs\objects" `
  "$BackupRoot\lfs-objects" `
  /E /COPY:DAT /DCOPY:DAT /R:2 /W:2

Get-FileHash "$BackupRoot\ShooterRoyal.bundle" -Algorithm SHA256
```

`robocopy` 返回码 `0` 至 `7` 视为成功，`8` 及以上视为失败。备份目标必须是新目录，不使用 `/MIR` 或 `/PURGE`。

离线恢复演练：

```powershell
$RestorePath = "C:\dev\migration-verification\ShooterRoyal-offline-$Stamp"
$env:GIT_LFS_SKIP_SMUDGE = '1'
git clone "$BackupRoot\ShooterRoyal.bundle" $RestorePath
Remove-Item Env:\GIT_LFS_SKIP_SMUDGE

robocopy `
  "$BackupRoot\lfs-objects" `
  "$RestorePath\.git\lfs\objects" `
  /E /COPY:DAT /DCOPY:DAT /R:2 /W:2

git -C $RestorePath lfs checkout
git -C $RestorePath lfs fsck
git -C $RestorePath status --short
```

### 4.4 忽略文件审计

运行：

```powershell
git status --ignored --short
```

只单独备份无法重建且确实需要的本地文件。默认不备份：

- `Binaries/`
- `Intermediate/`
- `DerivedDataCache/`
- `.vs/`
- `Saved/Logs/`
- UnrealBridge 自动生成的 token 文件

不得把账号 token、EOS/Steam 私密凭据或机器级密钥放进 Git 快照。

### 4.5 UnrealBridge 仓库快照

ShooterRoyal 快照通过后，再为 `C:\dev\UnrealBridge` 建立独立开发基线：

```powershell
Set-Location C:\dev\UnrealBridge

git diff --check
git diff --stat
git switch -c "codex/ue58-unrealbridge-$Stamp"
git add -A
git commit -m "chore: snapshot UnrealBridge before UE 5.8.1 integration"
```

提交前必须确认 3 个现有生成文件只有预期的时间戳和 hash 更新，不得覆盖或回退它们。

---

## 5. 目标架构

```text
Codex / CLI / MCP client
        |
        +-- UnrealBridge wrapper / grouped adapter
        |       |
        |       +-- TCP/UDP discovery
        |       +-- token-auth HTTP Job endpoint
        |       +-- durable Job / polling / idempotency
        |       +-- ChangeSet / package ownership / save whitelist
        |
        +-- UE 5.8 official Unreal MCP (optional direct read/tool discovery)
                |
                +-- Tool Search
                +-- ToolsetRegistry
                +-- Epic shipping Toolsets

UnrealBridgeUE58 adapter
        |
        +-- discover and classify official Toolsets
        +-- safely invoke reusable tools through non-blocking Jobs
        +-- normalize schemas/results/errors
        +-- add high-level transactional operations where official tools are insufficient
```

### 5.1 架构决策

1. UnrealBridge 继续作为修改任务的权威执行通道。官方 Unreal MCP 默认只作为能力源和可选直连通道。
2. 官方 MCP 默认 Tool Search 已解决“把所有 tool schema 常驻上下文”的主要 token 问题，因此不再采用旧路线图中“只注册一个自由文本 `exec_python`”的方案。
3. UnrealBridge 只向 ToolsetRegistry 注册少量结构化高层工具，例如 capability、Job、ChangeSet 和 grouped operation，不急切注册全部 1179 个函数。
4. 对官方 Toolset 的调用优先走进程内 registry/adaptor。若其 async contract 需要后续 Editor Tick，必须转换成 UnrealBridge polling Job，禁止 GameThread 同步等待 future。
5. 若进程内 API 不稳定或会死锁，退回为外部客户端分别连接两个 endpoint；禁止 UnrealBridge 在 GameThread 内发 HTTP 请求等待同一 Editor 的官方 MCP。
6. 官方 MCP 没有认证，只允许 loopback。UnrealBridge 的 token-auth endpoint 保持不变，不降低安全基线。
7. UE 5.8 专属代码隔离在条件编译的适配模块或适配层中，不把 Experimental API 扩散进 34 个现有 library。

---

## 6. 官方能力审计方法

UE 5.8.1 安装后，不依据预览版或旧路线图硬编码 Toolset 数量。Batch 2 必须从运行中的 5.8.1 Editor 导出实际结果：

1. `initialize`
2. `tools/list`
3. `list_toolsets`
4. 对每个 Toolset 执行 `describe_toolset`
5. 保存 tool name、参数 schema、返回 schema、模块、Experimental 状态、只读/修改推断
6. 对每个 Tool 标记 `Reuse / Wrap / Extend / Reject`

分类标准：

| 分类 | 采用条件 | UnrealBridge 动作 |
|---|---|---|
| Reuse | 官方实现完整、类型明确、无保存或事务风险 | 文档和 wrapper 直接复用 |
| Wrap | 官方实现可用，但缺少 durable Job、统一错误或能力发现 | 薄封装并保留官方逻辑 |
| Extend | 缺少高层意图、验证、事务、rollback 或 ShooterRoyal 所需深度 | 增加 UnrealBridge 原生高层操作 |
| Reject | 会盲目保存、破坏性删除、依赖模糊匹配或容易阻塞 GameThread | 不暴露给正式工作流 |

审计产物：

- `tests/fixtures/ue58-toolsets.json`
- `docs/ue58-toolset-capability-matrix.md`
- manifest 中的 `provider`, `engine_min`, `risk`, `execution`, `save_behavior`

---

## 7. 能力扩展范围

以下范围不只包括 Niagara。实际实现以 Batch 2 的 5.8.1 schema 审计为准。

| 领域 | UE 5.8 可借力点 | UnrealBridge 计划补充 | 优先级 |
|---|---|---|---|
| Slate / UMG | Slate snapshot、screenshot、Editor 内事件交互 | Widget runtime 状态、可访问性快照、UMG 与 Slate 对照、截图断言 | P0 |
| Automation / Build | Automation Controller、Live Coding、官方测试 Toolset | 测试发现/运行/轮询/报告、golden image、构建健康检查 | P0 |
| Blueprint | Actor/Object core tools、现有 5.7+ 节点能力 | 安全 node create/connect、pin default、latent/async/GAS task、compile diff | P1 |
| Material | Material Instance tools、现有 45 项 5.7+ 实现 | Graph 原语、高层母材质意图、compile/lint、事务和参数回读 | P1 |
| GameFeature | 官方 GameFeature toolset 候选 | 状态、依赖、activation/deactivation、Action 审计和失败诊断 | P1 |
| GAS / GameplayTags | 官方 GAS/Tag toolsets 候选 | Ability/Effect/AttributeSet 高层 CRUD、task node、安全 redirect 和验证 | P1 |
| Data | DataTable、通用 UObject 反射 | CSV/JSON 导入、UserDefinedStruct/Enum、typed row diff、引用校验 | P1 |
| AI / StateTree | StateTree、WorldConditions、AI toolsets 候选 | BT/BB/EQS 运行时状态、StateTree binding/transition、批量验证 | P1 |
| Niagara | 官方 Niagara toolset 候选 | User Parameter、Emitter rename、nested Dynamic Input、颜色/尺寸/生命周期绑定、stage/persist/rollback | P1 |
| Animation | 官方 Animation toolset、现有 PoseSearch/Chooser/IK | AnimGraph、Montage、ControlRig、IK Retarget、Motion Matching 高层操作 | P2 |
| Sequencer | SequencerTools | binding、track、section、key、spawnable/possessable、ControlRig bake 验证 | P2 |
| PCG / Geometry | PCG Toolset、现有 PCG/Geometry library | Graph CRUD、generation Job、attribute/schema 验证、Geometry transaction | P2 |
| Landscape / Foliage | 现有 Bridge 长任务与采样 | 5.8 回归、World Partition 协同、批量验证和性能预算 | P2 |
| Physics / Dataflow | 官方 Physics/Dataflow toolsets 候选 | Collision、constraint、PhysicsAsset、Dataflow graph 的类型化高层接口 | P2 |
| Audio / MetaSound | UObject/graph 反射基础 | MetaSound graph、参数、编译、AudioComponent 与 SoundCue 验证 | P2 |
| Networking | 现有 Replication library | replication/dormancy/RPC/NetUpdate、双客户端采样和回归报告 | P2 |
| Source Control | 现有 SourceControl 薄接口 | Git LFS 状态、checkout/conflict、修改资产与提交文件对应关系 | P2 |

明确要求：每个领域先完成 read、validate、compile，再开放 write；先开放 create/update，再讨论 destructive delete。删除资产、Blueprint 节点或生产数据不进入本计划默认能力。

---

## 8. 分批执行计划

### Batch 0：快照与迁移基线

内容：

1. 完成第 4 节 ShooterRoyal 和 UnrealBridge 双仓库快照。
2. 记录 UE 5.6.1 最终 build、PIE、Dirty Package、manifest/hash 基线。
3. 确认 UE 5.8.1 迁移副本与 5.6.1 原项目物理路径不同。
4. 确认 5.8.1 Lyra 基线对照完成。

退出标准：G0 至 G6 全部通过。

### Batch 1：UE 5.8.1 兼容与真实构建

内容：

1. 使用 UE 5.8.1 Launcher Build 执行 clean `BuildPlugin`。
2. 构建 ShooterRoyal 的 `LyraEditor Win64 Development`，先修第一个真实错误。
3. 审计 5.8.0 到 5.8.1 API drift，重点检查 Anim、GAS、PoseSearch、Chooser、PCG、Geometry、Material 和 JSON converter。
4. 只在必要位置增加 `UE_VERSION_OLDER_THAN` shim，保留 UE 5.3 至 UE 5.7 编译路径。
5. 修正文档中错误的 5.4/5.7 gate 描述，并把 matrix 精确更新到 5.8.1。
6. 同步插件到 ShooterRoyal，启动 Editor，执行 ping、health、capabilities。
7. 重新生成 UE 5.8.1 manifest/wrapper 并验证严格 hash 握手。

退出标准：

- UE 5.8.1 BuildPlugin 成功。
- ShooterRoyal Editor Target 成功。
- Bridge `ready=true`。
- 一个只读调用、一个不保存修改、一个 polling Job 均通过。
- UE 5.6.1 至少重新跑一次主体回归；完整旧版本 matrix 在发布批执行。

### Batch 2：官方 Unreal MCP 与 ToolsetRegistry 清单审计

内容：

1. 经确认后启用 `ModelContextProtocol`、`ToolsetRegistry` 和选定 Toolsets；初始不启用 Auto Start。
2. 导出实际 toolset/schema 清单并生成第 6 节审计产物。
3. 验证 Tool Search、`RefreshTools`、Codex client config 和 MCP Inspector。
4. 复现 5.8.1 已修复的两个 MCP 问题：无 SCS Blueprint 添加组件、`tools/call` response framing。
5. 测量短调用、长调用、并发误用和 Editor 忙碌时的延迟。

退出标准：所有 shipping Toolset 均完成 `Reuse / Wrap / Extend / Reject` 分类，无未审计的修改工具进入后续实现。

### Batch 3：UE 5.8 适配模块与 schema 联邦

预计范围：

- 新增条件编译的 `UnrealBridgeUE58` Editor 模块或等价隔离层
- 修改 `UnrealBridge.uplugin`
- 新增 `UnrealBridgeUE58.Build.cs`
- 扩展 manifest generator、wrapper、preflight 和 capability response

内容：

1. 接入 ToolsetRegistry 的 discovery API。
2. 将官方 tool schema 映射为 UnrealBridge manifest provider entry。
3. 暴露少量高层 Toolset：capability、Job、ChangeSet、grouped operation。
4. 避免 1179 个函数 eager 注册；依赖 Tool Search 按需发现。
5. 5.7 以下构建不引用任何 UE 5.8 Experimental header/module。

退出标准：官方和 UnrealBridge 能力可在一个 capability 查询中区分来源、版本、风险、保存行为；5.6.1 编译不受影响。

### Batch 4：官方 Toolset 的可靠 Job 适配

内容：

1. 官方同步短调用映射到 `GameThreadShort`。
2. 需要后续 Tick、编译、渲染或异步任务的调用映射到 polling Job。
3. 增加 queue deadline、取消、idempotency、断线结果恢复和 structured error normalization。
4. 禁止对修改工具盲目自动重试。
5. 记录 provider/toolset/tool、queue/run/total ms、side effect state 和 owned packages。
6. 健康检查在官方 Toolset 运行期间保持可响应。

退出标准：60 秒任务期间 100 次 health 成功；客户端超时后可以查询最终结果；修改工具的重复幂等键只执行一次。

### Batch 5：Slate、UMG、Automation 与验证闭环

内容：

1. 复用 SlateInspector 的 snapshot/screenshot/ref 模型。
2. 把 Editor 内 Slate 交互限制为 Unreal API，不使用 OS 鼠标、键盘或窗口自动化。
3. UMG Widget Tree 与运行时 Slate Tree 建立可比对引用。
4. 增加 Automation test discovery/run/status/result/stop 的 durable wrapper。
5. 增加 golden-image baseline、差异图、阈值和 artifact 输出。
6. 增加 Blueprint/Widget compile-all、MapCheck 和日志关键字聚合。

退出标准：可以对一个 ShooterRoyal UI 流程完成“打开/运行 -> Slate 快照 -> 截图 -> 断言 -> 自动化报告”，最终无无关 Dirty Package。

### Batch 6：Gameplay、Data 与 AI

内容：

1. GameFeature 状态、依赖、Action 和 activation 失败诊断。
2. GameplayTag source/redirect、GAS Ability/Effect/AttributeSet 与 AbilityTask 高层接口。
3. DataTable CSV/JSON 导入、row diff、UserDefinedStruct/Enum typed field。
4. Blueprint async/latent/GAS task node、安全 pin connect 和 compile diff。
5. BT/Blackboard/EQS runtime read、StateTree state/task/transition/binding 验证。
6. Networking replication/dormancy/RPC/NetUpdateFreq 只读审计与双客户端采样。

退出标准：每类至少有 read、validate、一个不保存 write smoke 和一个错误输入测试；服务器权威相关操作不得由客户端直接修改权威状态。

### Batch 7：Niagara、Material 与 Graph 高层意图

Niagara 必做：

1. 创建、读取、重命名 typed User Parameter。
2. 安全重命名 Emitter，并验证所有 handle/reference。
3. 添加已有 Emitter、Module 和 nested Dynamic Input。
4. 生命周期、颜色、尺寸、速度等参数绑定。
5. typed intent、stage、persist、compile、validate、rollback。
6. 失败时返回具体 stack/module/input path，不使用模糊字符串匹配静默选中错误节点。

Material 必做：

1. 5.7+ 的 45 项实现完整回归。
2. expression create/connect/output、parameter、Material Instance 和 compile error。
3. 高层母材质配置使用可预览 ChangeSet，不直接保存。
4. lint、unused node、static switch explosion 和 shader compile 状态。

退出标准：Niagara 的五个 User Parameter、Emitter rename、Dynamic Input 和颜色/尺寸/生命周期绑定可在临时测试资产上完整创建、编译、回读和 rollback；Material graph 同样具备可回滚 smoke。

### Batch 8：Animation、Sequencer、World、Physics 与 Audio

内容：

1. Animation/AnimGraph/Montage、PoseSearch、Chooser、IK Rig/Retarget、ControlRig。
2. Sequencer binding、track、section、key、spawnable/possessable、ControlRig bake 验证。
3. PCG graph CRUD、generation Job、schema/attribute 验证。
4. Landscape、Foliage、World Partition 和 Navigation 的 5.8.1 回归与批量验证。
5. PhysicsAsset、constraint、collision、Dataflow graph。
6. MetaSound graph、parameter、compile 和 AudioComponent 验证。

退出标准：每个实际存在官方 Toolset 的领域完成复用或扩展；不存在稳定 API 的项目明确标记 deferred，不用 raw private-field hack 假装完成。

### Batch 9：发布、同步与持续验证

内容：

1. 跑 UE 5.3 至 UE 5.8.1 BuildPlugin matrix。
2. 跑 Python tests、UE Automation、MCP protocol、Job soak、ChangeSet 和 capability snapshot tests。
3. 更新 README、SKILL、API references、version compatibility 和 changelog。
4. 重新生成并比对 manifest/wrapper。
5. 同步到 ShooterRoyal 插件，进行 Editor build、PIE、MapCheck 和最终 Dirty audit。
6. 版本号只在兼容性和 API diff 确认后决定，不预先强制 major bump。

退出标准：全部 Required batch 通过，ShooterRoyal 中只保留明确授权的目标 Dirty Package，最终 PIE=false、Bridge ready=true、manifest/hash 一致。

---

## 9. 测试矩阵

| 类别 | 必测项 | 通过条件 |
|---|---|---|
| Build | UE 5.8.1 BuildPlugin | clean pass |
| Project | `LyraEditor Win64 Development` | exit code 0，无 UHT error |
| Compatibility | UE 5.3 至 5.8.1 matrix | 全部受支持列 pass |
| Handshake | plugin/registry/manifest/wrapper hash | 完全一致，否则拒绝执行 |
| Official MCP | initialize/list/describe/call/refresh | MCP Inspector 与 Codex 均通过 |
| Hotfix regression | BP no-SCS component、tools/call framing | 不崩溃、不丢结果 |
| Job | 60 秒 polling + 100 次 health | 100/100 health，Job ID 稳定 |
| Disconnect | 客户端中途断开 | 重连后恢复最终结果 |
| Idempotency | 相同 key 重复修改 | 只执行一次 |
| ChangeSet | preview/commit/rollback | preview 不 dirty，rollback 恢复 |
| Save policy | 存在无关 dirty package | 不保存、不覆盖无关 package |
| Blueprint | compile-all + invalid pin/node | 成功项通过，错误结构化 |
| UMG/Slate | snapshot/screenshot/interaction | ref 稳定，无 OS 输入注入 |
| Niagara | parameter/emitter/dynamic input/binding | compile、回读、rollback 通过 |
| Material | graph create/connect/compile | compile、diff、rollback 通过 |
| Automation | discover/run/status/result/stop | 长测试不阻塞 health |
| Networking | Listen Server + 2 clients | 权威、复制、RPC 基本回归通过 |
| Security | endpoint bind/auth/origin | 官方 MCP 仅 loopback；Bridge token 有效 |

测试资产默认创建在测试插件或临时 package，不写入生产 `/ShooterRoyal/**`。确需使用 ShooterRoyal 自定义资产时，必须另行列出精确资产白名单、操作、保存策略和验证方式。

---

## 10. 每批交付要求

每个 Batch 必须交付：

1. 修改前问题分析、目标文件、Header/Build/Config/Content 风险和验证方案。
2. 独立可审查的代码差异，不混入无关重构。
3. 对应的自动测试或可重复 smoke script。
4. UE 5.8.1 构建结果和必要的旧版本回归。
5. manifest/wrapper 生成与 hash 验证。
6. 同步到 ShooterRoyal 后的实际 Editor 验证。
7. Dirty Package、PIE、shader/asset compilation 和已知限制报告。
8. 文档状态与下一执行指针更新。

只 ping 成功不算完成。每批至少验证：

- 一个只读调用
- 一个不保存的修改调用
- 一个异步或 polling Job
- 一个错误输入
- 一个断线或超时恢复场景
- 最终 Dirty Package 白名单

---

## 11. 安全与停止条件

1. 不在原始 UE 5.6.1 项目上直接转换或保存 5.8 资产。
2. 不删除 `.uasset`、`.umap`、Blueprint 节点或生产数据。
3. 不修改 Engine 源码来迁就 Experimental Toolset API。
4. 不把官方 MCP 暴露到非 loopback 网络。
5. 不允许官方修改工具绕过 ChangeSet、package ownership 或保存白名单。
6. 不在 GameThread 使用 `Sleep`，也不等待依赖后续 Tick 的 future/HTTP response。
7. 不自动重试没有 idempotency 的修改调用。
8. 不覆盖任务开始前已存在的 UnrealBridge 生成文件或 ShooterRoyal 用户改动。
9. 不以 Toolset 名称存在作为能力完成证据，必须实际调用、验证返回和副作用。
10. 同一真实阻塞连续出现三轮且无法通过适配层解决时，停止该领域并记录 deferred；不得用不安全 private-field hack 绕过。

---

## 12. 升级完成后的启动指针

用户完成 UE 5.8.1 升级后，下一次任务从以下只读检查开始：

1. 获取 UE 5.8.1 安装路径和迁移后的 ShooterRoyal 项目路径。
2. 读取两个仓库的 branch、HEAD、status、remote 和 LFS fsck。
3. 通过 UnrealBridge 读取 Editor version、project、level、PIE、Dirty Package 和 health。
4. 读取 UE 5.8.1 首次启动/编译日志中的第一个真实 error。
5. 检查 `ModelContextProtocol`、`ToolsetRegistry`、`AllToolsets` 是否存在，但不自动启用。
6. 核对 G0 至 G6。
7. 只有 Gate 全部通过后开始 Batch 1。

建议用户在升级完成后提供一句明确指令：

```text
UE 5.8.1 和对应 Lyra 已安装，ShooterRoyal 迁移副本可以打开。按 unrealbridge-ue58-upgrade-optimization-execution-plan.md 从 Batch 0 Gate 审计开始执行。
```

---

## 13. 参考资料

本地文档：

- [`unrealbridge-optimization-execution-plan.md`](./unrealbridge-optimization-execution-plan.md)
- [`eda-integration-roadmap.md`](./eda-integration-roadmap.md)
- [`agent-capability-gaps.md`](./agent-capability-gaps.md)
- [`agent-capability-gaps-extended.md`](./agent-capability-gaps-extended.md)
- [`blueprint-capability-roadmap.md`](./blueprint-capability-roadmap.md)
- [`material-capability-roadmap.md`](./material-capability-roadmap.md)
- [`../version-compatibility.md`](../version-compatibility.md)

Epic 官方资料：

- [Unreal MCP in Unreal Editor](https://dev.epicgames.com/documentation/unreal-engine/unreal-mcp-in-unreal-editor)
- [ToolsetRegistry API](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ToolsetRegistry)
- [SlateInspectorToolset API](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/SlateInspectorToolset/USlateInspectorToolset)
- [AutomationTestToolset API](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/AutomationTestToolset/UAutomationTestToolset)
- [Working with PCG and LLMs Using Unreal MCP](https://dev.epicgames.com/documentation/unreal-engine/working-with-pcg-and-llms-using-unreal-mcp-in-unreal-engine)
- [Unreal Engine 5.8 Release Notes](https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-8-release-notes)
- [UE 5.8.1 release and hotfix notes](https://forums.unrealengine.com/t/unreal-engine-5-8-released/2729274)
- [Updating Projects to Newer Versions](https://dev.epicgames.com/documentation/unreal-engine/updating-projects-to-newer-versions-of-unreal-engine)
- [Upgrading Lyra to the Latest Engine Release](https://dev.epicgames.com/documentation/unreal-engine/upgrading-the-lyra-starter-game-to-the-latest-engine-release-in-unreal-engine)

