---
sr_runner_schema: "SRTR-1"
sr_tutorial_id: "SR-DOC-001"
sr_tutorial_title: "UnrealBridge UE 5.8.1 可靠执行与上下文优化"
sr_tutorial_version: 2
sr_tutorial_status: "active"
sr_runner_enabled: true
sr_default_package: "UB-RELIABILITY-01"
sr_authority: "procedure"
---

# UnrealBridge UE 5.8.1 可靠执行与上下文优化执行计划

日期：2026-09-05。源码根目录：`C:/dev/UnrealBridge`。验证工程：`C:/dev/ShooterRoyal_5_8_DirectUpgrade/ShooterRoyal.uproject`。引擎：`C:/dev/UnrealEngine-5.8.1-release`。

本轮六阶段执行已完成；实际结果与限制见 [验收报告](<../002_UnrealBridge_UE581_可靠执行优化验收报告(done).md>)。本文保留稳定的 SRTR-1 Step ID 和 active 操作合同，执行进度仍由独立证据维护。

用户已批准的顺序为 **World 定位 → 异步 Trace → 目录更新 → 项目上下文 → 紧凑图格式 → 运行验收**。本文件固定范围与完成合同；实际进度、授权答复和证据写入独立的 [执行状态与恢复指针](../../.tmp/codex-handoffs/ub-reliability-20260905.md)，不以本文 Checkbox 表示完成。

2026-09-05 授权补充：用户明确确认“后续阶段直接执行不需要我再额外授权”。本次六阶段所需 Header、实现、必要依赖调整、正常构建、Editor 重启和验证均在总授权内，适用 AGENTS 2.7。下文 Gate 保留其操作类型与审计含义；本次执行依恢复指针中的授权记录直接通过，不重复请求授权。Content 保存白名单保持为空，范围之外的新功能不在此授权内。

## 范围、基线与恢复规则

- 基于 UnrealBridge 3.0.0 / protocol 2 的现有实现增量修改；已完成的 [UE 5.8 升级计划](unrealbridge-ue58-upgrade-optimization-execution-plan.md) 不重新执行。
- 本次不包括 AnimGen、AnimDatabase、Mutable、Concert、安装商业插件或新增第三方账号服务。
- 已确认源码问题：Networking 按名称跨 World 返回首个 Actor；Gameplay 和 Enhanced Input 分别选首个 PIE World；Perf 的四条 Trace 解析路径同步调用 `Analyze`；MCP 的索引与官方目录在模块导入时固定。
- 已有基础必须复用：Job、ChangeSet、官方 Toolset policy、SemanticSearchToolset、Blueprint 摘要与 ApplyGraphOps、Scenario、Automation、Artifacts。发现能力不等于允许执行该能力。
- 初始 Git：分支 `codex/ue58-unrealbridge-20260826-173148`，HEAD `9fe6b5e`，91 个状态条目（54 tracked / 37 untracked）。已有改动属于工作基线，禁止覆盖、提交或回滚。
- 首次只读采样：Editor `5.8.1-0+UE5`，Bridge `ready=true`，地图 `/ShooterRoyal/Maps/SR_Field`，PIE=false，Dirty=[]，Shader/Asset 编译任务均为 0。该记录不能代替后续实时预检。
- 用户已明确提供六阶段总授权；新增/修改 Header 与必要构建调整在实施前列明 API、原因和验证方案，随后直接执行。不是从“计划通过”推定总授权，而是依据 2026-09-05 的明确追加指令。
- Content 写入和保存白名单为空。验证仅允许只读检查、明确说明的临时 PIE 及插件构建；不保存地图或资产，不删除 Blueprint 节点，不操作资产文件。
- 每个阶段开始读本文、恢复指针、目标文件真实内容及定向 Git diff；先完成前置阶段再执行后置阶段。用户已委托六阶段，取得各阶段必要 Gate 后可以连续推进，不能扩展到本文以外功能。
- 每次重要修改/验证结束及约 15 次工具调用更新恢复指针：当前阶段、具体下一动作、已获授权、修改清单、Job ID、构建/运行证据、Dirty/PIE 和未知结果。压缩后从指针续接，不重做已完成操作。
- 进程退出、超时或上下文压缩后，先查真实 Job 与文件状态；结果未知的写操作不能直接重试。本文不承诺 Job 可跨 Editor 重启自动恢复。
- `apply_patch` 修改文本。安装副本同步只复制经校验的本次源文件，并备份每个被覆盖文件；不调用会扫描/复制 Content 的全插件同步脚本。
- 失败时保留证据并修复明确错误；不擅自回滚。Header/反射改动使用正常 Unreal Build；仅函数体修改优先 Live Coding。任何版本/manifest 漂移都必须修复到 `ready=true`，不能绕过预检。

## 阶段与交付

| 顺序 | 交付 | 核心验收 |
|---|---|---|
| 01 | World/Actor 明确定位 | 歧义拒绝；三 World 精确命中；旧句柄失效；作用域不串任务 |
| 02 | Trace 异步分析 | 大 Trace 不阻塞 GameThread；状态/取消真实；摘要有界 |
| 03 | 在线目录更新 | 注册变化可见；多 Editor 缓存隔离；未知工具仍拒绝 |
| 04 | 项目上下文 | C++/资产/配置证据可追溯；增量刷新；结果有预算 |
| 05 | 紧凑图表达 | 节点/Pin/连接/默认值保真；切片完整标记；版本冲突拒绝 |
| 06 | 运行验收封装 | 单机/三 World 自动断言；失败证据；可核查恢复与收尾 |

<!-- SR-RUNNER:PACKAGE
{"id":"UB-RELIABILITY-01","order":10,"availability":"active","entry_step":"UB-WORLD-01","requires":[],"completion_policy":"all_required_steps","manual_package_advance":true}
-->
## World 定位

<!-- SR-RUNNER:STEP
{"id":"UB-WORLD-01","package_id":"UB-RELIABILITY-01","order":10,"availability":"active","actor":"codex","interaction":"guided","validation":"automatic","risk":"high","gates":["header_confirm"],"requires":[],"next":["UB-TRACE-01"],"skippable":false,"completion_review":"codex_postcheck_required"}
-->
### 修复误选并建立 World/Actor 句柄

#### 本步目标

- 你正在做什么：使网络检查、角色观察与语义输入明确落在指定的 Editor、服务器或客户端 World。
- 结果会用在哪里：用于 ShooterRoyal 正式玩法的多客户端诊断与后续自动运行验收，不改游戏逻辑或复制规则。
- 为什么这样调/选：Listen Server 与两个 Client 会出现同名 Actor；名称不足以决定目标，旧 PIE 的路径还可能在下一次 PIE 重用。

#### 开始前

- `CODEX_PREFLIGHT`：定向比较 Networking/Gameplay 源码与安装副本 Hash；读取 Editor/PIE/Dirty，保留既有用户改动。
- `USER_READY`：`.cpp` 的歧义防护已在实施授权范围内；新增 World 反射 Header 需确认下文的通知。

#### 现在操作

1. 修改 `Plugin/UnrealBridge/Source/UnrealBridge/Private/UnrealBridgeNetworkingLibrary.cpp`：完整路径精确匹配；短名称/Label 出现多个不同 Actor 时返回失败并给出诊断，不在事务开始后才发现歧义。
2. 修改 `Plugin/UnrealBridge/Source/UnrealBridge/Private/UnrealBridgeGameplayLibrary.cpp`：默认仅接受唯一已开始运行的 PIE World；Enhanced Input 使用同一解析入口。此阶段多 World 的无选择器调用会失败，这是可观察的兼容性收紧。
3. Header 确认后新增 World 服务：以 Editor 会话 nonce、World 弱引用和对象弱引用生成不透明句柄；World 销毁、Actor 重实例化、PIE 重开或插件重载后，旧句柄必须拒绝。完整路径是当前调用的精确目标，不声称它具备跨 PIE 的身份稳定性。
4. 在一次 Job 的执行切片内建立显式 World 作用域；Python 用 `try/finally` 退出，原生 Job 边界再次清理。Networking 与 Gameplay 使用同一作用域；持续输入和 ticker 必须捕获具体 World/PlayerController 弱引用，不能在后续 tick 重新选默认 World。Actor 句柄不能越过 World 作用域。
5. 只同步本阶段声明的源文件，构建后刷新 manifest/包装器；使用当前地图临时启动 Listen Server + 两个 Client，测试精确路径、歧义拒绝、句柄失效、任务作用域隔离，最终停止本次创建的 PIE。

#### 自动化与人工边界

- Codex 完成定位、实现、构建、参数回读和测试。用户仅处理新的 Header Gate；运行测试不代表游戏手感或网络质量已经人工验收。
- 不创建/保存 Content；World 数量为 3 是为了同时覆盖服务器与两个独立客户端，不作为性能基准。

#### 验证与完成标准

- 第一部分：真实三 World 下短名称失败、完整 Actor 路径回读精确相等；无 PIE 返回空，单 PIE 行为保留；歧义拒绝没有修改 Actor。
- 完整阶段：同名 Actor 可以借句柄逐 World 区分；错 World、旧 Actor、旧 PIE、旧 Session 均失败；作用域在异常、取消、Job 切片结束后不泄漏；持续输入仅作用于目标实例。
- 构建成功，manifest/registry 一致且 `ready=true`；PIE=false，Dirty 与起始基线一致。只有完整标准通过才记录 `UB_WORLD_PASS`，仅误选防护通过不代表整个阶段完成。

#### 停止条件

Header 未确认不写 `.h`；发现未知 Dirty 或需要 Content/Config/Build 改动时，先按精确范围处理对应 Gate；任何目标身份歧义都不执行写入。

#### Header 修改通知

- 目标：新建 `Plugin/UnrealBridge/Source/UnrealBridge/Public/UnrealBridgeWorldLibrary.h`，对应新建 `Private/UnrealBridgeWorldLibrary.cpp`。
- 新公开 API：`GetWorldContexts`（返回有界 World 描述与不透明句柄）、`BeginWorldScope(WorldHandle)`、`EndWorldScope(ScopeToken)`、`ResolveActorReference(WorldHandle, ActorNameOrPath)`、`ValidateActorReference(ActorHandle)`。外部描述用 JSON 字符串，句柄为字符串；不向调用方暴露裸指针。
- 非反射内部接口：获取当前执行 World、解析受限 Actor 句柄、清理当前执行作用域，供 Networking、Gameplay 与 Job 边界使用。
- 原因：需要让 Python/MCP 显式设置并校验 World 身份，新增原生接口必须经 UHT 暴露；只改 `.cpp` 只能拒绝歧义，不能提供完整的句柄接口。
- 同步影响：上述两个现有 `.cpp`、`Private/UnrealBridgeServer.cpp` 的 Job 执行边界，以及 Python MCP/作用域辅助模块、manifest 和生成包装器。既有函数签名保留，旧名称匹配改为歧义失败。
- 不改变游戏类继承、Replication、RPC、GAS 或资产结构，预计无需新增模块依赖；如实际需要 Build/其他 Header，先追加精确通知，不扩大本通知授权。
- 验证：正常构建 `LyraEditor Win64 Development`，重启后重新生成 manifest/包装器并检查版本一致，再执行前述三 World 测试。重建/重启前再次确认 Dirty=[]、PIE=false 及明确重启授权。

<!-- SR-RUNNER:PACKAGE
{"id":"UB-RELIABILITY-02","order":20,"availability":"active","entry_step":"UB-TRACE-01","requires":["UB-RELIABILITY-01"],"completion_policy":"all_required_steps","manual_package_advance":true}
-->
## 异步 Trace

<!-- SR-RUNNER:STEP
{"id":"UB-TRACE-01","package_id":"UB-RELIABILITY-02","order":10,"availability":"active","actor":"codex","interaction":"guided","validation":"automatic","risk":"high","gates":["header_confirm"],"requires":["UB-WORLD-01"],"next":["UB-CATALOG-01"],"skippable":false,"completion_review":"codex_postcheck_required"}
-->
### 让 Trace 分析与 Editor 帧循环解耦

#### 本步目标

- 你正在做什么：把性能、内存、网络和 Cook Trace 分析改为可轮询任务。
- 结果会用在哪里：用于正式地图和联机诊断的离线性能证据，防止读取大 Trace 时冻结 Editor。
- 为什么这样调/选：UE 5.8.1 已提供 `StartAnalysis` 和分析 Session；耗时遍历也必须离开 GameThread，仅替换启动函数不足以消除阻塞。

#### 开始前

- `CODEX_PREFLIGHT`：确认 `UB_WORLD_PASS`，检查本机 TraceServices 头文件中的线程/锁与取消语义，准备大小不同的可读 Trace 样本。
- `USER_READY`：对 `Public/UnrealBridgePerfLibrary.h` 新增接口及重建方式提供当前阶段的 Header 确认。

#### 现在操作

1. 修改 `Private/UnrealBridgePerfLibrary.cpp` 与 `Public/UnrealBridgePerfLibrary.h`：拟新增 `StartTraceAnalysis`、`GetTraceAnalysisStatus`、`GetTraceAnalysisResult`、`CancelTraceAnalysis`；实施前固定参数、结果 schema、资源上限并发出该 Header 的精确通知。
2. 用 `StartAnalysis` 启动引擎分析；后台进行有界摘要处理并遵守 provider/session 的锁协议。状态包含等待、分析、摘要、成功、失败、取消请求和取消完成；不能用“收到取消”冒充已停止。
3. 复用现有四类摘要逻辑与 Artifacts，限制并发数、文件大小、保留时间和 Top N；公开轮询只读取快照，不等待 `Analyze`、`Wait` 或全量 provider 遍历。
4. MCP 默认走异步接口，旧 `Parse*` 入口明确标记兼容限制；不得保留被默认工具调用的同步 GameThread 重路径。Editor 退出时释放任务且结果明确为中断，不宣称跨重启恢复。

#### 自动化与人工边界

Codex 负责样本准备、耗时测量、取消和失败测试。无可用大样本时报告样本限制，不以小文件推断大型工程表现。仅写 Trace/报告文件，不写 Content。

#### 验证与完成标准

正常/损坏/不存在文件均有终态；摘要与现有可信样本对齐；分析期间独立 GameThread ping 持续响应，记录基线、最大延迟和样本大小；取消后不继续发布成功结果；重复获取结果一致、资源可释放。通过后记录 `UB_TRACE_PASS`。

#### 停止条件

无法确认 TraceServices 锁或 Session 生命周期时不启动后台 provider 访问；Header 未确认、GameThread 阻塞或取消状态虚报时不得进入下一阶段。

<!-- SR-RUNNER:PACKAGE
{"id":"UB-RELIABILITY-03","order":30,"availability":"active","entry_step":"UB-CATALOG-01","requires":["UB-RELIABILITY-02"],"completion_policy":"all_required_steps","manual_package_advance":true}
-->
## 目录更新

<!-- SR-RUNNER:STEP
{"id":"UB-CATALOG-01","package_id":"UB-RELIABILITY-03","order":10,"availability":"active","actor":"codex","interaction":"automatic","validation":"automatic","risk":"medium","gates":[],"requires":["UB-TRACE-01"],"next":["UB-CONTEXT-01"],"skippable":false,"completion_review":"codex_postcheck_required"}
-->
### 用当前 Editor 的目录驱动发现

#### 本步目标

- 你正在做什么：使工具搜索随官方 Toolset 注册、卸载和 Editor 会话变化更新。
- 结果会用在哪里：为正式工程的后续上下文检索与操作选择提供真实可用能力。
- 为什么这样调/选：当前静态 fixture 适合离线审计，但不能代表每个 Editor 实际加载的工具；实时发现还必须与已审计策略相交。

#### 开始前

- `CODEX_PREFLIGHT`：确认 `UB_TRACE_PASS`；核实 `ToolsetRegistry` 的变化通知及原生 Catalog 输出。
- `USER_READY`：无额外人工前置。若实现确需 Header/Build 改动，先补精确 Gate 再执行该改动。

#### 现在操作

1. 修改 `.claude/skills/unreal-bridge/scripts/unreal_bridge_mcp_server.py`、`unreal_bridge_workflows.py`、`unreal_bridge_domains.py` 及必要的 `Private/UnrealBridgeUE58Library.cpp`。
2. 用 project + endpoint + Editor Session + catalog revision 分隔缓存；统一 ToolIndex、DomainRegistry、搜索、描述和实际调用的目录快照。fixture 只作为明确标注的离线结果。
3. 接入原生变化通知或版本检测；短时缓存、失效、并发刷新与分页 cursor 绑定同一 revision，目录变化时旧 cursor 返回可理解的失效错误。
4. 把 live discovery 与 exact-id/schema-hash policy 分开；新工具、schema 漂移和已卸载工具不能通过过期索引进入执行。

#### 自动化与人工边界

Codex 运行离线多 Session/并发测试及真实 Editor 目录对照。不能通过修改官方工具权限来制造通过结果。

#### 验证与完成标准

两个模拟 Editor 相互隔离；注册/卸载/重启后刷新可见；断线带明确 stale/offline 状态；未审计工具保持拒绝；线上数量与官方目录一致。通过后记录 `UB_CATALOG_PASS`。

#### 停止条件

刷新失败不得伪造最新目录；策略 hash 不匹配不得绕过；发生跨项目缓存复用时停止实际调用。

<!-- SR-RUNNER:PACKAGE
{"id":"UB-RELIABILITY-04","order":40,"availability":"active","entry_step":"UB-CONTEXT-01","requires":["UB-RELIABILITY-03"],"completion_policy":"all_required_steps","manual_package_advance":true}
-->
## 项目上下文

<!-- SR-RUNNER:STEP
{"id":"UB-CONTEXT-01","package_id":"UB-RELIABILITY-04","order":10,"availability":"active","actor":"codex","interaction":"automatic","validation":"automatic","risk":"medium","gates":[],"requires":["UB-CATALOG-01"],"next":["UB-GRAPH-01"],"skippable":false,"completion_review":"codex_postcheck_required"}
-->
### 聚合可追溯的项目证据

#### 本步目标

- 你正在做什么：将源码、配置、Blueprint/资产引用与现有语义搜索合并成一个有预算的上下文结果。
- 结果会用在哪里：支持正式玩法问题的调用链定位和变更影响分析，减少反复读取大型文件与资产。
- 为什么这样调/选：单独的语义相似度不能证明调用关系，结果必须保留精确路径、符号、引用方向和采样版本。

#### 开始前

- `CODEX_PREFLIGHT`：确认 `UB_CATALOG_PASS`，检查 SemanticSearchToolset 实际可用性、资产引用与 Blueprint 摘要接口。
- `USER_READY`：无额外人工前置。

#### 现在操作

1. 新建 `.claude/skills/unreal-bridge/scripts/unreal_bridge_project_context.py`，在 MCP 服务增加上下文查询与影响分析入口，参数至少包含 query、target paths、max_items 和输出预算。
2. 本机检索限于明确项目根；结合 C++ 符号/调用文本、Config 字段、Asset Registry 依赖和 Blueprint 摘要。把已证明的引用与语义候选分别标明，保留来源与置信依据。
3. 缓存以文件 Hash/mtime、资产 Package 状态及会话 revision 更新；预算用尽明确截断与 continuation。缺少官方语义服务时返回降级原因和仍可用的结构化证据。
4. 新增针对跨根路径、删除/重命名、内容变化、结果排序和预算边界的行为测试；真实工程只读抽查一条可核对的代码—资产关系。

#### 自动化与人工边界

Codex 负责检索与证据核实，不上传源码，不引入另一个收费向量服务，不把整仓文本或全部资产写入模型上下文。

#### 验证与完成标准

结果中的每条关系可追溯到文件行/符号或 UE 引用 API；源文件变化后刷新；重复查询稳定；输出不超过声明预算；缺失/过期证据显式标记。通过后记录 `UB_CONTEXT_PASS`。

#### 停止条件

越过声明项目根、发现敏感配置内容或无法区分推测与引用证据时停止该结果输出；不为补充上下文修改源码或资产。

<!-- SR-RUNNER:PACKAGE
{"id":"UB-RELIABILITY-05","order":50,"availability":"active","entry_step":"UB-GRAPH-01","requires":["UB-RELIABILITY-04"],"completion_policy":"all_required_steps","manual_package_advance":true}
-->
## 紧凑图格式

<!-- SR-RUNNER:STEP
{"id":"UB-GRAPH-01","package_id":"UB-RELIABILITY-05","order":10,"availability":"active","actor":"codex","interaction":"automatic","validation":"automatic","risk":"medium","gates":[],"requires":["UB-CONTEXT-01"],"next":["UB-RUNTIME-01"],"skippable":false,"completion_review":"codex_postcheck_required"}
-->
### 用紧凑且可校验的结构表达 Blueprint 图

#### 本步目标

- 你正在做什么：为图检查和变更提案提供保留节点身份的紧凑格式及语义差异。
- 结果会用在哪里：用于正式玩法 Blueprint 的理解、影响检查和将来的受限编辑，减少重复字段造成的上下文消耗。
- 为什么这样调/选：省略显示布局可以节约体积，但 Pin 类型、连接方向、默认值和未知节点字段不能被默默丢掉。

#### 开始前

- `CODEX_PREFLIGHT`：确认 `UB_CONTEXT_PASS`；核对现有 Blueprint graph/pin 导出是否足够保真，以及 ApplyGraphOps 的逐条失败语义。
- `USER_READY`：本阶段仅导出、diff、dry-run，不需要 Content 写入。若原生读取不足需加 Header，先报告精确接口并确认。

#### 现在操作

1. 新建 `.claude/skills/unreal-bridge/scripts/unreal_bridge_graph_codec.py`，定义版本化节点、Pin、边、默认值、引用字典、graph revision 和完整/切片标记；layout 作为可选字段。
2. 提供导出、局部邻域切片、解码与语义 diff；未知节点保留扩展字段，不声称支持缺失信息的无损回写。
3. 变更提案绑定 expected graph revision，先校验节点/Pin/类型，再映射至已有 ApplyGraphOps；只交付 dry-run 结果，明确该底层接口不是失败自动全回滚的原子编辑器。
4. 以离线图 fixture 做往返、类型/默认值保真、跨切片边界和冲突测试；真实工程只读采样图，记录原始与紧凑 JSON 字节数。只有具备实际 tokenizer 时才报告 token 节约比例。

#### 自动化与人工边界

Codex 可导出、比较和生成编辑提案；本阶段不删除节点、不写 Blueprint、不保存资产。正式编辑另需精确资产与操作授权。

#### 验证与完成标准

完整图的受支持字段可往返一致；切片不伪装成全图；陈旧 revision 被拒绝；多种节点样本的压缩率与局限有证据。通过后记录 `UB_GRAPH_PASS`。

#### 停止条件

出现 GUID/Pin 冲突、未知类型信息丢失或无法验证 revision 时禁止生成可执行变更；不为测试创建 Content。

<!-- SR-RUNNER:PACKAGE
{"id":"UB-RELIABILITY-06","order":60,"availability":"active","entry_step":"UB-RUNTIME-01","requires":["UB-RELIABILITY-05"],"completion_policy":"all_required_steps","manual_package_advance":true}
-->
## 运行验收

<!-- SR-RUNNER:STEP
{"id":"UB-RUNTIME-01","package_id":"UB-RELIABILITY-06","order":10,"availability":"active","actor":"codex","interaction":"automatic","validation":"automatic","risk":"medium","gates":[],"requires":["UB-GRAPH-01"],"next":[],"skippable":false,"completion_review":"codex_postcheck_required"}
-->
### 封装可追溯的运行检查

#### 本步目标

- 你正在做什么：把 World 选择、语义动作、条件等待、断言和证据收集组成命名验收场景。
- 结果会用在哪里：用于正式玩法每次修改后的机器可判定回归，输出可供后续任务继续核查的报告。
- 为什么这样调/选：一次成功调用不代表玩法正确；单机与三 World 需要分别记录身份、状态、超时和断言结果。

#### 开始前

- `CODEX_PREFLIGHT`：确认前五阶段 PASS，核对当前地图、Dirty、Job 队列与测试要求，确认不会接管用户已有 PIE。
- `USER_READY`：机器验收无额外人工前置。视觉、听感或手感不在本次自动通过范围。

#### 现在操作

1. 基于 `.claude/skills/unreal-bridge/scripts/unreal_bridge_workflows.py` 的 ScenarioManager 与现有 Automation/Artifacts，增加明确的场景 schema、World selector、条件式等待、超时、断言及证据索引。
2. 通过 UE API 执行语义输入，不注入桌面鼠标键盘。使用 Job 轮询推进；每步记录输入 Hash、World/Session、Job ID、结果与副作用状态。
3. 将恢复状态区分为可继续、已完成、需核对、不可恢复；Editor 重启后不能把旧 Job ID 当作仍在执行，超时与 abandon 不能冒充底层已经取消。
4. 提供只读基线和 Listen Server + 两个 Client 示例；输入动作另设精确运行范围。`finally` 只停止本任务创建的 PIE，保存白名单保持为空；失败保留日志片段/Trace/截图等真实证据。

#### 自动化与人工边界

Codex 自动运行机器断言、整理证据及清理自己创建的临时运行状态。自动截图只能证明画面采样，不能替代用户审美和手感判断。

#### 验证与完成标准

成功、断言失败、超时、取消、断线/重启的报告均与真实状态一致；幂等重试不重复已知完成步骤；未知副作用先核查；最终 PIE/Dirty/Shader/Asset compile 状态齐全。通过后记录 `UB_RUNTIME_PASS`，六阶段全部通过后才提交最终完成报告。

#### 停止条件

遇到未授权资产操作、无法证明 World 身份、用户已有 PIE 或副作用未知时暂停依赖操作；不跨阶段追加新的产品功能。

## 证据与文档校验

- 每阶段测试和测量写入 `C:/dev/UnrealBridge/.tmp/ub-reliability/`，恢复指针链接精确文件；记录测试对象与样本大小，区分离线测试、正常构建、Live Coding 和真实 Editor 运行。
- 本仓已有 11 个历史 Markdown 均无编号；本计划开始使用 `001`，不批量重命名旧文档。编号审计应报告历史基线，不将其冒充本次新增缺号。
- 使用 `C:/dev/SRReader/scripts/` 的 SRTR Build/Check/Test 对本计划做独立配置、独立输出目录校验；不得修改 SRReader 正式内容清单或发布产物。额外检查本仓 Markdown 相对链接、编号、重复 ID、状态后缀。
- 交付必须列出本次文件、构建/测试结果、未验证项与最终 Dirty/PIE，并保留下一机械动作。所有阶段完成前，文档保持 active，不添加 `(done)`。

## 设计参考

这些参考只解释功能取舍，不作为实际实现已验证的证据：Aura 的 [项目上下文](https://www.tryaura.dev/documentation/project-understanding/) 与 [性能分析](https://www.tryaura.dev/documentation/performance-profiling/)，BAIB 的 [紧凑图表示](https://xrvp.io/tools/baib-blueprint-ai-bridge/)，Ludus 的 [插件工作流界面](https://docs.ludusengine.com/getting-started/plugin-interface/)。实际以本地 UE 5.8.1 源码、UnrealBridge 调用链与测量结果为准。
