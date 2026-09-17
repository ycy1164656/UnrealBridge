# UnrealBridge 3.0 发布说明

> 版本：`3.0.0`
>
> 协议：`2`（保持线协议兼容）
>
> 发布日期：`2026-08-27`
>
> 支持与发布验收目标：仅 Unreal Engine `5.8.1`

## 发布结论

UnrealBridge 3.0 不只是 UE 5.8 编译适配。本版本完成了旧会话和
[`unrealbridge-ue58-upgrade-optimization-execution-plan.md`](plans/unrealbridge-ue58-upgrade-optimization-execution-plan.md)
约定的完整能力层：统一发现与搜索、结果裁剪、Artifact、可恢复 Scenario、Automation
闭环、Blueprint 继承组件解析、运行时 UMG/Slate、Gameplay/AI、Niagara、Sequencer、
StateTree、Material、MetaSound、Control Rig、PCG、Physics、Dataflow、Mesh、World
Partition 和多客户端网络验收。

3.0 的产品支持范围固定为 UE `5.8.1`。UE 5.6 不在 3.0 的安装、构建、回归或
维护范围内；仓库中保留的 pre-5.8 条件编译分支与历史 2.x clean BuildPlugin 记录，
只用于追溯和降低未来代码整理风险，不构成 3.0 兼容性声明。

UE 5.8 官方 Experimental `ToolsetRegistry` 作为第二 provider 接入；它不替换
UnrealBridge 已有的 TCP/HTTP MCP、durable Job、ChangeSet、鉴权、幂等、取消、断线
恢复和保存白名单。

## 2.0 到 3.0 的核心升级

### 统一发现、查询与大结果

- 新增 `bridge_list_domains`、`bridge_search_tools`、`bridge_describe_tools`，统一检索
  UnrealBridge manifest、3.0 高层领域操作和 UE 5.8 官方 Toolset。
- 所有 grouped 结果支持 `_fields`、`_omit`、`max_items`、`cursor`；分页 cursor 绑定
  查询 hash，不能跨查询误用。
- 大 JSON、截图、日志、trace、golden diff 和工作流报告进入 content-addressed
  Artifact store，可分块读取，避免 MCP 上下文被大结果淹没。
- manifest/wrapper 扩展为 `39` 个 library、`1218` 个 UFUNCTION、`5` 个 enum；
  manifest hash 为
  `10e82a452f798587c221c0654beb32dcaa6e187e9acf0acdeb447be55946ba47`，
  registry hash 为 `5a238582b56869a45c7c4ecd97027b39`。

### Durable Job、Scenario 与 Automation

- 官方 Toolset 和 typed workflow 都复用 durable polling Job：支持 queue deadline、
  cooperative cancel、idempotency、结构化错误、状态查询和断线后结果恢复。
- 新增持久化 Scenario 状态机：逐步状态、断言、只对安全步骤重试、失败回滚 hook、
  Artifact 证据和客户端重连恢复。
- 新增 Automation durable wrapper：测试发现、启动、状态、结果、停止和报告 Artifact
  构成闭环；不在 GameThread 内等待依赖后续 Tick 的任务。
- 新增 golden-image 比较，输出指标、JSON 报告和 PNG diff Artifact。

### UE 5.8 官方 Toolset 精确执行策略

UE 5.8.1 运行态审计得到 `53` 个 Toolset、`832` 个 Tool。3.0 不再把 776 个工具
整体搁置，而是对每个 `toolset|tool` 做精确策略绑定，并把结构化 input schema hash
纳入授权判断：

| 执行面 | 数量 | 3.0 行为 |
|---|---:|---|
| `ReadOnly` | 387 | 审计后的查询；不保存 |
| `RuntimeInteraction` | 49 | PIE、Automation、GameFeature、Slate/Sequencer 等运行态操作；必须显式 opt-in，不保存 |
| `TransactionalSync` | 326 | 非破坏性同步修改；要求显式目标、立即完成、ChangeSet 归属与失败回滚，不保存 |
| `Rejected` | 70 | 删除、任意脚本、外部文件/路径、Source Control、显式保存或无法纳入事务的操作 |

策略默认拒绝：策略文件没有精确记录、schema hash 变化、执行面参数不满足或目标包不明确
时均不执行。调用者不能用自报 risk 绕过策略。AST 源码审计还会识别官方实现中的显式
`save_asset/save_actor/save_assets/save_dirty_packages` 调用并强制拒绝。

完整逐工具审计见
[`ue58-toolset-capability-matrix.md`](ue58-toolset-capability-matrix.md)。

### 领域能力补全

| 领域 | 3.0 新增或深化能力 |
|---|---|
| Blueprint | native CDO、当前 Blueprint SCS、继承链 SCS 的组件解析；ICH resolver；安全节点/Pin、compile diff 与无保存回滚 |
| UMG / Slate | 运行时 Widget/Slate 快照、稳定引用、语义动作、状态回读、截图与 golden；只用 Unreal API，不注入 OS 鼠标键盘 |
| GameFeature / Data | 状态、依赖、Actions、activation/deactivation 验证与恢复；DataRegistry 和既有 DataTable/typed row 能力接入统一目录 |
| AI | BT、Blackboard、EQS、Perception、Smart Object 资产与运行时审计；StateTree state/task/evaluator/condition/transition/binding/parameter 的创建、编译、验证和回滚 |
| Niagara | typed User Parameter、Emitter identity/rename、Module、nested Dynamic Input、生命周期/颜色/尺寸等绑定、compile/validate/rollback |
| Material / MetaSound | 高层 graph intent、节点/连接/参数、编译验证、lint 和 ChangeSet 回滚；MetaSound 运行态 AudioComponent 审计 |
| Sequencer | possessable/spawnable、track、section、channel/key、选择与求值、结构验证、编译/回读/rollback |
| Control Rig / PCG | schema-hashed typed workflow；Control Rig 图/层级操作；PCG 图修改、generation Job 和 attribute/schema 验证 |
| Physics / Dataflow / Mesh | PhysicsAsset/constraint/collision、Dataflow graph、Static/Skeletal Mesh 的高层 typed workflow 与无保存事务边界 |
| World | World Partition/Data Layer 状态、Streaming Source、验证；Landscape/Foliage/Navigation 沿用并完成 5.8 API 适配 |
| Networking | replication/dormancy/RPC/NetUpdateFreq 审计，PIE world/NetDriver/role/connection 采样，以及临时 Listen Server + 2 clients 启动 API |

## UE 5.8.1 兼容性改动

- 插件版本升为 `3.0.0`，Protocol 保持 `2`。
- UE 5.8 官方 Toolset 依赖限定在版本条件分支；3.0 的支持与发布 Gate 只覆盖
  UE 5.8.1。pre-5.8 分支即使仍能条件编译，也属于未支持的遗留路径。
- 移除已并入 `CoreUObject` 的独立 `StructUtils` 模块依赖。
- 适配 `ForEachObjectWithOuter`、Landscape `SetAlphaData`、PCG data type、GPU
  profiler、Material usage 和 PoseSearch 5.7+ unified animation asset API。
- UE 5.8.1 `LyraEditor` 最终构建中 UnrealBridge 自身的 C4996 deprecation warning
  已清零。
- MCP Python 依赖固定为 `mcp>=1.6.0,<2`；修复 Python 3.14 下 `vswhere`
  输出解码。

## 验证记录

- UE 5.8.1 clean `BuildPlugin`：通过（51 秒）。
- `C:\dev\ShooterRoyal_5_8_DirectUpgrade` 的
  `LyraEditor Win64 Development`：通过。
- UE Automation `UnrealBridge.*`：`5/5` 通过。
- Python test suite：`54 passed, 7 skipped`。
- HTTP MCP protocol：`7/7` 通过。
- 实际 stdio MCP：启动成功，`92` 个 MCP tools、`61` 个 domains；发现、分页、
  describe、Artifact、live ping 均通过。
- 60 秒 polling soak：`100/100` health、0 failure、0 running-job mismatch；断线恢复
  与幂等通过；mean `323.93 ms`、p95 `330.88 ms`、max `411.19 ms`。
- 主要 live smoke 全部通过：Scenario rollback、transactional graph、Material/
  MetaSound intent、Control Rig、Physics/Mesh、Niagara、StateTree、Sequencer、
  Slate/UMG、Gameplay systems、Automation、Listen Server + 2 clients。
- 多客户端网络 smoke 确认 1 个 Listen Server 与 2 个 Client world、有效 NetDriver、
  双客户端连接路径、Autonomous/Simulated proxy、共同复制的 `SRGameState`，并在 finally
  停止 PIE、恢复原 Dirty 状态。

## 安装与同步

1. 运行 `sync_plugin.bat [目标 Plugins\UnrealBridge 路径]`；默认目标为
   `C:\dev\ShooterRoyal_5_8_DirectUpgrade\Plugins\UnrealBridge`。同步为非破坏式，
   不使用 `/MIR`，不复制 `Binaries/Intermediate/__pycache__`。
2. 正常构建项目的 Editor Target。
3. 运行 `install_codex_skill.bat`。安装器先复制 `.claude` 中的运行时脚本和 references，
   再覆盖 `.codex` 的 Codex 专用 `SKILL.md`，不会先删除用户目录。
4. Codex 的 `~/.codex/config.toml` 将 MCP adapter 依赖固定为
   `mcp>=1.6.0,<2`，随后重启 Codex 以重新加载 server 配置。

## 已知限制与边界

- UE 5.6 及其他 pre-5.8.1 引擎不受 UnrealBridge 3.0 支持，也不属于发布阻塞项；
  不为这些引擎承诺安装、编译、运行或回归结果。
- 旧版 UE 5.3–5.7 的条件编译路径与历史 clean BuildPlugin 记录仍保留，但仅作历史
  资料，不能用来宣称 3.0 支持这些版本。
- 官方 Toolset 的 70 个 `Rejected` 操作是有意的安全边界，不是“待补功能”；需要保存、
  删除、外部文件或 Source Control 的任务必须另建具备明确授权和恢复策略的专用工作流。
- 当前 ShooterRoyal 关卡审计中 EQS 与 Smart Object 实例数量为 0，表示当前地图没有相应
  实例，不表示接口未实现。
- Niagara 临时模板可能报告空 stack 的非致命 validation warning；结构、编译状态和
  rollback 已通过，未把 warning 隐藏成成功。
