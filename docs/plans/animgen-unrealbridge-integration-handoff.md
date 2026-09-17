# AnimGen 与 UnrealBridge 集成交接

> 文档状态：调研完成，尚未实施  
> 交接日期：2026-09-05  
> UnrealBridge 工作仓库：`C:\dev\UnrealBridge`  
> ShooterRoyal UE 5.8.1 项目：`C:\dev\ShooterRoyal_5_8_DirectUpgrade\ShooterRoyal.uproject`  
> UE 源码：`C:\dev\UnrealEngine-5.8.1-release`  
> 目标：在不破坏 UnrealBridge 3.0 可靠性边界的前提下，评估并分阶段增加 AnimGen 数据准备、配置、训练、接入和验证能力。

## 1. 本次交接结论

Epic AnimGen 已随当前 UE 5.8.1 源码安装，但默认关闭，版本为 `0.1`，状态为 Experimental。它不是文本生成任意动作的云服务，而是使用已有 Animation Database/动捕数据，在 Editor 中训练 AutoEncoder 和 Controller，并由训练后的神经网络在运行时根据控制输入逐帧生成角色姿态。

AnimGen 适合与 UnrealBridge 结合，但职责应严格分离：

```text
Codex + UnrealBridge
  = 数据盘点、资产配置、训练任务编排、AnimGraph 接入、自动验证和报告

AnimGen
  = Editor 内训练与 Runtime 本地神经网络推理
```

UnrealBridge 不应参与每帧动画推理，也不应让游戏运行时依赖 MCP/TCP/HTTP。训练完成后，Runtime 只使用 AnimGen Controller、AutoEncoder 和 NNE 网络资产。

当前 UnrealBridge 3.0 没有 AnimGen 专用 C++ library、manifest 方法、高层 domain 或 UE 5.8 官方 Toolset 条目。对源码、manifest 和官方 tool catalog 的字符串审计均为 `0` 个 AnimGen 命中。因此，本功能仍处于“可设计、尚未实现”状态。

## 2. 当前工作现场

### 2.1 UnrealBridge

- 仓库：`C:\dev\UnrealBridge`
- 分支：`codex/ue58-unrealbridge-20260826-173148`
- HEAD：`9fe6b5e`
- Origin：`https://github.com/ycy1164656/UnrealBridge.git`
- 插件版本：`3.0.0`
- Protocol：`2`
- 当前发布说明记录：`39` 个 library、`1218` 个 UFUNCTION、`5` 个 enum。
- 本文档新增前 `git status --porcelain` 为 `90` 项；新增本文档后为 `91` 项，包含大量 tracked 和 untracked 的 UnrealBridge 3.0 工作成果。

这些既有改动不是本次 AnimGen 调研产生的。新会话不得执行 `git reset`、`git restore`、`git checkout --`、清理 untracked 文件、整库格式化或用远端内容覆盖本地工作树。

### 2.2 ShooterRoyal

- 项目：`C:\dev\ShooterRoyal_5_8_DirectUpgrade\ShooterRoyal.uproject`
- 分支：`5.8`
- HEAD：`749a622d`
- EngineAssociation：`{E8430EBC-4691-40CD-9289-C4AB4FD68AA1}`
- 当前 `git status --porcelain`：约 `199` 项，包含 Config、Source 和大量 `.uasset` 改动。
- `ShooterRoyal.uproject` 中没有显式 `AnimGen` 条目，因此 AnimGen 尚未为项目启用。
- `ShooterRoyal.uproject` 中也没有显式 `UnrealBridge` 条目；当前 Bridge 通过项目内插件的默认启用设置运行。

交接时 UE 5.8.1 Editor 状态：

- 进程：`UnrealEditor.exe`，PID `12216`。
- Bridge TCP：`127.0.0.1:12171`。
- Bridge HTTP MCP：`http://127.0.0.1:11438/mcp`。
- Bridge：`ready=true`、`healthy`、队列为空。
- Registry hash：`5a238582b56869a45c7c4ecd97027b39`。
- Manifest hash：`10e82a452f798587c221c0654beb32dcaa6e187e9acf0acdeb447be55946ba47`。
- Wrapper version：`10e82a452f798587`。
- Editor：UE `5.8.1-0+UE5`、`PIE=false`、未暂停。
- 当前地图：`/ShooterRoyal/Maps/SR_Field`。
- 打开的资产：`0`。
- Dirty Package：`[]`。

上述进程状态只代表 2026-09-05 交接时刻。新会话必须重新执行发现与状态检查，不能假设 PID、端口或 Dirty 状态仍相同。没有用户在当前任务中的明确要求，不得停止 Editor、安排关机或执行关机。

## 3. AnimGen 本机事实

### 3.1 插件声明

插件文件：

`C:\dev\UnrealEngine-5.8.1-release\Engine\Plugins\Experimental\Animation\AnimGen\AnimGen.uplugin`

已确认：

- `VersionName = 0.1`
- `IsExperimentalVersion = true`
- `EnabledByDefault = false`
- Runtime module：`AnimGen`
- UncookedOnly module：`AnimGenEditor`

声明依赖：

- `PythonFoundationPackages`
- `PythonMLPackages`
- `AnimationWarping`
- `LearningCore`
- `NNERuntimeBasicCpu`
- `AnimDatabase`
- `TensorBoard`
- `DrawDebugLibrary`
- `PoseSearch`
- `UAF`
- `UAFAnimGraph`
- `UAFPoseSearch`

官方资料：

- Plugin API：<https://dev.epicgames.com/documentation/unreal-engine/API/PluginIndex/AnimGen?lang=en-US>
- Epic Community Tutorial：<https://dev.epicgames.com/community/learning/tutorials/eGME/unreal-engine-animgen>
- AnimGen Example：<https://www.fab.com/listings/df37eb46-09bf-4604-9307-cdc39c769790>

官方样例包含两个方向：

- 可重新训练的 Player Controller，覆盖基本 locomotion、手枪 aiming 和 firing，并附训练数据。
- 预训练的多风格 NPC Controller，支持大量步行风格和 props；样例没有附训练数据，不能在 Editor 中重新训练。

### 3.2 Runtime 资产与节点

关键源码：

- `Engine\Plugins\Experimental\Animation\AnimGen\Source\AnimGen\Public\AnimGenAutoEncoder.h`
- `Engine\Plugins\Experimental\Animation\AnimGen\Source\AnimGen\Public\AnimGenController.h`
- `Engine\Plugins\Experimental\Animation\AnimGen\Source\AnimGen\Public\AnimGenControl.h`
- `Engine\Plugins\Experimental\Animation\AnimGen\Source\AnimGen\Public\AnimGenBehavior.h`
- `Engine\Plugins\Experimental\Animation\AnimGen\Source\AnimGen\Public\AnimNode_AnimGenController.h`

`UAnimGenController` 是 `UDataAsset`。源码注释将其定义为使用 flow-matching 神经网络、自回归逐帧生成姿态的已训练控制器。Controller 资产包含：

- `UAnimGenAutoEncoder` 引用。
- Control Schema 和 Control Schema Element。
- Control Encoder 网络。
- LOD0、LOD1、LOD2 推理网络。
- 训练帧率、控制向量尺寸及归一化/分布数据。
- Editor-only 的训练输入哈希、网络尺寸统计、TrainingSettings 和 ViewportSettings。

`FAnimNode_AnimGenController` 可放入 AnimGraph，主要输入为：

- Controller。
- Control Object 与 Control Object Element。
- LOD level。
- Seed、Evaluation Period、Force Evaluation。
- Pose/Attribute smoothing。

节点还可以输出 Curve、Attribute、指定 Bone、Anim Notify 和 Anim Notify State，并接入 Root Motion Provider。这使它能参与脚步、姿态标签、附加骨骼输出和表现事件，但不意味着这些生成事件可以直接作为多人战斗服务器权威判定。

### 3.3 训练流程

关键源码：

- `Engine\Plugins\Experimental\Animation\AnimGen\Source\AnimGenEditor\Public\AnimGenEditorTraining.h`
- `Engine\Plugins\Experimental\Animation\AnimGen\Source\AnimGenEditor\Private\AnimGenEditorAutoEncoderToolkit.cpp`
- `Engine\Plugins\Experimental\Animation\AnimGen\Source\AnimGenEditor\Private\AnimGenEditorControllerToolkit.cpp`
- `Engine\Plugins\Experimental\Animation\AnimGen\Content\Python\train_autoencoder.py`
- `Engine\Plugins\Experimental\Animation\AnimGen\Content\Python\train_controller.py`

Controller 训练的大致流程由源码明确描述为：

1. 从 Animation Database 提取帧和 sequence range。
2. 通过 AutoEncoder 将每帧姿态编码为向量。
3. 调用 Behavior/Control Schema 为每帧构造控制向量。
4. 准备 reference flow-matching/denoiser 网络与 LOD distillation 网络。
5. 写出 JSON 训练配置并启动 Python/PyTorch 子进程。
6. 通过共享内存与子进程交换进度、网络和结果。
7. 将训练结果回填 Controller 资产。

训练设置包含 Database、FrameRanges、AutoEncoder、Behavior、网络层数/宽度、BatchSize、LearningRate、训练和 distillation iteration、RandomSeed、Device、logging 和 TensorBoard。源码支持 CPU/GPU 选择；CPU 可以运行但通常非常慢，GPU 应作为正式训练默认路径。

## 4. 当前最大的技术限制

### 4.1 训练生命周期不是公开 UFUNCTION

`FControllerTrainingModel` 和 `FAutoEncoderTrainingModel` 定义在 `AnimGenEditor` 的 Private `.cpp` 内。`StartTraining()`、`StopTraining()`、进度、Loss、ETA、子进程和回填逻辑由资产 Editor Toolkit 私有模型持有。

结果是：

- 重新生成 UnrealBridge manifest/wrapper 不会自动出现完整训练接口。
- 通用 UObject 属性写入只能配置 TrainingSettings，不能安全启动和托管完整训练。
- 直接运行 `train_controller.py` 不等价于完整 Editor 训练，因为训练前数据提取、共享内存 schema、网络创建和训练后资产回填都在 C++ Toolkit 中。
- 通过 Slate/按钮模拟点击不应成为正式方案：它依赖窗口状态，难以幂等、恢复和可靠取消。
- 复制数百行 Private Toolkit 训练实现到 UnrealBridge 会形成高维护成本分叉，不应直接开始。

### 4.2 需要单独的架构 Gate

完整训练自动化前必须在以下方案中做决定：

| 方案 | 优点 | 缺点 | 当前建议 |
|---|---|---|---|
| 仅管理训练外围，人工点击 Train | 不改 Engine，最快落地 | 无法完全无人值守 | 第一阶段可接受 |
| 向 AnimGen 增加最小公共 Training Service | 复用 Epic 原实现，语义最可靠 | 形成 UE 源码补丁，升级需维护 | 经过原型验证后优先评估 |
| 在 UnrealBridge 重建训练 orchestration | Bridge 可完全控制 | 大量重复私有逻辑，易随 5.8.x 变化 | 不推荐作为首选 |
| Slate UI 自动化 | 开发表面成本低 | 脆弱、不可恢复、难验证 | 禁止作为正式实现 |

新会话在这个 Gate 之前不得宣称“可以完整自动训练”。

## 5. 可复用的 UnrealBridge 3.0 能力

当前已有底座可以覆盖训练前后大部分工作：

- `Asset`、generic factory、property/reflection：资产发现、创建原型、读取和设置公开属性。
- `Anim`：AnimBlueprint/AnimGraph 读取、通用节点创建、节点连接、状态机和编译。
- `PoseSearch`：数据库条目、采样范围、镜像设置、索引构建和状态轮询。
- `IK`/Retargeter：骨架、链映射和训练素材预处理。
- `Sequencer`：对照镜头、Skeletal Animation section 和录制/评测辅助。
- durable Job：幂等键、断线恢复、取消、状态和结果持久化。
- Scenario：多步断言、失败处理、rollback hook 和结果汇总。
- Artifact：训练日志、配置、模型统计、TensorBoard 路径和评测报告。
- Automation/PIE/Perf：编译、运行时 smoke、性能和回归测试。

现有 `Anim.add_anim_graph_node_by_class_name` 理论上可以创建 `UAnimGraphNode_AnimGenController`，但尚未在真实 AnimGen 资产上验证节点初始化、Controller 属性绑定、Control Object pin 和编译结果。不能把“通用节点可创建”写成“AnimGen 接入已完成”。

## 6. 建议新增的 AnimGen 领域接口

建议新增独立高层 domain `animgen`，而不是让调用方拼接通用反射和任意 Python。

### 6.1 Read-only

- `animgen.list_assets`
- `animgen.inspect_autoencoder`
- `animgen.inspect_controller`
- `animgen.inspect_training_settings`
- `animgen.validate_dataset`
- `animgen.validate_autoencoder`
- `animgen.validate_controller`
- `animgen.get_training_requirements`
- `animgen.get_runtime_compatibility`

返回值至少包含 asset path、class、skeleton、database、frame ranges、behavior、schema hash、trained content hash、frame rate、network sizes、LOD、valid/stale 状态和错误列表。

### 6.2 Transactional configuration

- `animgen.create_autoencoder`
- `animgen.create_controller`
- `animgen.configure_autoencoder`
- `animgen.configure_controller`
- `animgen.set_training_database`
- `animgen.set_frame_ranges`
- `animgen.set_behavior`
- `animgen.set_network_profile`
- `animgen.set_training_profile`

要求：

- 默认 `save=false`。
- 所有目标必须是精确资产路径。
- 先 preview，后 commit。
- 使用 `Modify()`、ChangeSet ownership 和白名单保存。
- 不暴露任意 Python script path 或 shell command。
- 训练配置写入后重新调用 AnimGen `Update()`/validation，避免只改属性而不刷新派生数据。

### 6.3 Durable training

目标接口：

- `animgen.start_autoencoder_training`
- `animgen.start_controller_training`
- `animgen.get_training_status`
- `animgen.cancel_training`
- `animgen.get_training_result`
- `animgen.promote_candidate_model`

训练 Job 必须记录：

- idempotency key。
- Controller/AutoEncoder 目标路径。
- Dataset、FrameRanges、Behavior、Control Schema 和配置 hash。
- RandomSeed、Device、iteration 和网络 profile。
- Python 子进程 PID/handle。
- 当前阶段：prepare、train、distill、import、validate、complete。
- iteration、max iteration、Loss、ETA、日志和 TensorBoard artifact。
- cancellation 是否已请求、子进程是否退出、资产是否被修改。

客户端超时只能返回可继续查询的 Job ID，不能重复启动 GPU 训练。训练逻辑不得在 GameThread 中 `sleep` 或长时间阻塞。

训练结果不得直接覆盖最后一个已验证模型。建议输出到候选资产或候选网络槽位，以 config hash 命名；只有评测通过后才显式 promote。

### 6.4 AnimGraph integration

- `animgen.add_controller_node`
- `animgen.configure_controller_node`
- `animgen.connect_control_inputs`
- `animgen.configure_outputs`
- `animgen.validate_animgraph_integration`

专用接口应验证 Controller/AutoEncoder/Skeleton 一致性、Control Schema、LOD、Root Motion、Curve/Notify 输出名和节点编译结果，而不是只返回“节点已创建”。

### 6.5 Evaluation

- `animgen.run_dataset_reconstruction_eval`
- `animgen.run_runtime_motion_eval`
- `animgen.compare_with_baseline`
- `animgen.capture_perf_profile`
- `animgen.export_evaluation_report`

建议指标：

- root trajectory position/yaw error。
- foot sliding 与 foot contact consistency。
- pose discontinuity/jerk。
- hand/prop alignment error。
- control response latency。
- curve/attribute/notify timing。
- 单角色和多 NPC 的 CPU/GPU 推理开销。
- Client/Server 下 root motion、位置复制和视觉一致性。

## 7. 模块组织建议

不要立即把 AnimGen 和 AnimGenEditor 硬依赖塞进 UnrealBridge 核心模块。

优先评估：

1. 新增可选 Editor module，例如 `UnrealBridgeAnimGen`。
2. 该模块只在 UE 5.8.1 且 AnimGen 可用时注册 `animgen` provider。
3. Core UnrealBridge 只负责 Job、ChangeSet、Artifact、权限和 domain dispatch。
4. 未启用 AnimGen 时，能力发现返回明确的 `PLUGIN_DISABLED`，而不是启动时失败。
5. `BuildPlugin` 必须同时覆盖 AnimGen enabled/disabled 的打包和启动情况。

是否需要修改 UnrealBridge `.uplugin`、增加 module、添加 `AnimGen`/`AnimGenEditor`/`AnimDatabase`/`LearningCore` Build 依赖，必须在首个实现批次前单独给出精确差异和构建影响。不要在调研批次顺带修改。

## 8. 建议执行批次

### Batch A：只读能力与兼容性探针

范围：

- 检测 AnimGen 插件存在、启用、module load 和版本。
- 列出/检查 AutoEncoder、Controller、Database、Behavior。
- 输出 skeleton、训练 hash、valid/stale 状态和依赖检查。
- 不创建资产、不训练、不改 AnimGraph、不保存。

验收：

- AnimGen disabled 时返回结构化错误。
- 空项目、官方 AnimGen Example 和 ShooterRoyal 均有测试结果。
- 不增加 Dirty Package。
- 新增 C++/Python contract tests 和至少一次 UE 5.8.1 live smoke。

### Batch B：创建与配置

范围：

- 类型化创建 AutoEncoder/Controller。
- 设置 Database、FrameRanges、Behavior 和训练 profile。
- ChangeSet preview/commit/rollback。

验收：

- 只修改声明资产。
- 默认不保存。
- rollback 后属性和 Dirty ownership 正确。
- 重新打开资产后设置一致。

### Batch C：训练生命周期原型与架构 Gate

范围：

- 证明是否能通过稳定公共 API 启动、轮询、取消并回填一次最小训练。
- 如果不能，停止编码并形成“最小 Engine patch”与“Bridge 重建 orchestration”的差异评审。

验收：

- 不允许以 Slate 点击作为通过条件。
- 客户端断线后 Job 仍可查询。
- 同一 idempotency key 不重复训练。
- cancel 后子进程和共享内存被清理。
- 训练失败不覆盖已验证网络。

### Batch D：AnimGraph 接入

范围：

- 创建并配置 AnimGen Controller 节点。
- 连接 Control Object 输入和输出 pose。
- 配置 LOD、平滑、Curve/Notify/Attribute 输出。
- 编译、引用和 Skeleton 校验。

验收：

- compile 无 Error。
- 重开资产后节点、pins 和属性保持。
- 失败时不删除用户已有节点。
- 不在未批准情况下保存 AnimBlueprint。

### Batch E：ShooterRoyal Pilot

建议先选择单个低风险 NPC locomotion，不立即替换玩家完整 Lyra/GASP 动画栈。

候选：

- 一个小兵的 idle/walk/run/turn/stop。
- 第二阶段再评估持枪 locomotion、aiming 和 firing pose。

边界：

- 攻击命中、换弹、技能窗口和 GAS/服务器伤害仍由现有权威逻辑控制。
- 生成的 Notify 首先只用于表现，不直接触发权威伤害。
- 现有 AnimBP/Animation Layer 保留回退路径。
- 先处理 Skeleton、IK 和训练数据授权，再训练。
- 视觉、脚感、枪感和动作语义需要人工验收。

## 9. 新会话首轮动作

新会话首先只执行以下只读检查：

```powershell
git -C C:\dev\UnrealBridge status --short
git -C C:\dev\ShooterRoyal_5_8_DirectUpgrade status --short

$Bridge = 'C:\dev\UnrealBridge\.claude\skills\unreal-bridge\scripts\bridge.py'
python $Bridge list-editors
python $Bridge --project 'C:\dev\ShooterRoyal_5_8_DirectUpgrade\ShooterRoyal.uproject' ping
python $Bridge --project 'C:\dev\ShooterRoyal_5_8_DirectUpgrade\ShooterRoyal.uproject' health
```

随后读取：

- 本文档。
- `C:\Users\ycy\.codex\skills\unreal-bridge\SKILL.md`。
- `C:\dev\UnrealBridge\docs\unrealbridge-3.0-release-notes.md`。
- `C:\dev\UnrealBridge\docs\plans\unrealbridge-ue58-upgrade-optimization-execution-plan.md`。
- 本机 AnimGen 的 `.uplugin` 与第 3 节列出的源码。

新会话应先报告：

- 工作树与 Editor 状态是否变化。
- Batch A 的精确目标文件和 API 草案。
- 是否需要新增 module/Build/plugin dependency。
- 测试和 live smoke 方案。
- 对现有 90/199 项改动的隔离办法。

在用户确认实施批次前，不启用 AnimGen、不修改 `.uproject`、不创建或保存 Content、不启动训练。

## 10. 可直接粘贴的新会话提示词

```text
请先读取 C:\dev\UnrealBridge\docs\plans\animgen-unrealbridge-integration-handoff.md
和 C:\Users\ycy\.codex\skills\unreal-bridge\SKILL.md。以
C:\dev\UnrealBridge 为 UnrealBridge 源码工作区，以
C:\dev\ShooterRoyal_5_8_DirectUpgrade\ShooterRoyal.uproject 作为 UE 5.8.1
live smoke 项目。先只读核对两个仓库的 git 状态、Editor/PIE/Dirty、Bridge
ping/health，以及 AnimGen 插件启用状态；不得清理或覆盖现有改动。然后给出
Batch A（只读能力与兼容性探针）的精确文件、API、风险和验证方案，等我确认后再写入。
没有我的当前任务明确指示，不要停止 Editor 或关机。
```

## 11. 本轮未完成事项

- 没有修改 UnrealBridge 或 ShooterRoyal 源码。
- 没有启用 AnimGen。
- 没有下载或安装 AnimGen Example。
- 没有创建 AutoEncoder、Controller、Database 或 AnimBlueprint 节点。
- 没有启动训练或运行 GPU/CPU benchmark。
- 没有验证通用 AnimGraph 节点创建对 `UAnimGraphNode_AnimGenController` 的实际兼容性。
- 没有决定是否维护 AnimGen Engine patch。
- 没有保存或关闭当前 Editor。
- 没有执行或安排关机。
