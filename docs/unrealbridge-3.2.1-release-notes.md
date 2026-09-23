# UnrealBridge 3.2.1 实施与验证记录

2026-09-21；Protocol 2；插件整数 Version 5（补丁，未升级执行平面）；Windows、UE 5.8。版本号描述当前源码与部署，不表示所有验收目标已通过。3.2.0 的既有边界与失败证据全部保留，未被本轮覆盖。

本轮范围为 `UnrealBridge_3.3.0_Upgrade_Execution_Plan.md` 中用户指定的 P0 与 P1；P2（Blender DCC 往返）、阶段 Y 与阶段 Z 未实施。

## 已实施

### P0：原生 Blueprint 片段往返

- 新增 `UnrealBridgeBlueprintFragmentLibrary`（6 个 UFUNCTION，`ToolIntroducedVersion 3.2.1`）与分组入口 `blueprint_fragment_op`：导出、检查、导入预检、导入、结构回读、列出可用 K2 图。
- 使用引擎自身的 `FEdGraphUtilities::ExportNodesToText / CanImportNodesFromText / ImportNodesFromText`，不引入第二套图序列化。
- 外部依赖由 `UK2Node::HasExternalDependencies()` 给出，不从引脚类型推断。
- **动态引脚与内部连线的保全以签名比对证明，而非分类猜测。** 每个节点产出 `pin_signature_sha256`（引脚名、方向、类型、默认值、连线数），连线记为 `(对端节点 GUID, 对端引脚名)` 以跨 GUID 重映射保持可比；回读产出 `structure_sha256` 供与源片段对比。运行时无法在不改图的前提下判定某引脚"是否动态"，因此不做该断言。
- 导入在 `FScopedTransaction` 内执行并调用 `Modify()`，标记 Blueprint 结构性修改，可选编译；**从不保存 package**，返回中固定携带 `package_saved: false`。
- `CanDuplicateNode()` 为假的节点（入口点等）列入 `skipped_non_duplicable`，不静默丢弃。
- 片段文本按不可信数据处理：SHA256 仅证明字节一致，不证明逻辑安全、来源授权或行为正确。

### P0：贴图导入（单工具窄提升）

- `editor_toolset.toolsets.texture.TextureTools|import_file` 由 `Rejected` 提升至 `TransactionalSync`，`save_behavior: Never`，要求显式 target packages。
- 生成器新增 `EXACT_EXTERNAL_SOURCE_IMPORTS` 类别。**未复用 `EXACT_INTERNAL_ASSET_MUTATIONS`**：该集合的不变量是"不读写任意外部路径"，而 `import_file` 正是读磁盘路径，归入其中等于谎报风险性质。
- 生成器校验所声明的源参数确实存在于审计 schema 中；不存在即构建失败，避免生成一个守卫永不匹配的策略。
- 原生侧新增 `UE58_AuthorizeExternalSourcePath`：源路径必须解析为授权根下的既存文件，拒绝路径穿越、UNC、设备命名空间、保留设备名与盘符相对形式；授权根默认为项目目录，可经 `UNREALBRIDGE_SOURCE_ROOTS` 显式放宽。引擎与插件目录不在默认授权根内。
- 守卫置于原生侧而非 Python wrapper：wrapper 无法约束直连原生入口的调用方。单次调用与批量调用两条路径均已接入。
- `static_mesh / skeletal_mesh / data_table / curve_table / string_table` 的 `import_file` **仍为 `Rejected`**；`AssetTools.delete` 仍为 `Destructive / Rejected`。

### P1：项目与关卡审计聚合

- 新增 `bridge_audit`（`unreal_bridge_audit.py`）：只读聚合，问题分为 `gameplay_fault` / `performance_lead` / `organization_suggestion`，每条带来源证据。
- 仅编排既有只读能力（`AssetTools.find_assets / get_dependencies / get_referencers / get_asset_class`），不做自动优化、删除、烘焙或移动。
- 截断总是上报（`truncated`）：一次看起来完整的部分扫描会让"无发现"失去意义。
- 依赖数量以"线索"而非"已测成本"表述；孤立资产的判定明确声明其作用域限制（域外、代码内与软路径引用不可见）。

### P1：命名审计与受控移动

- 新增 `bridge_content_organize`：`plan_moves` 从只读证据产出可审查计划，`apply_moves` 执行。
- 计划与执行分离：执行需同时回传计划、计划摘要与 `confirm=True`；摘要由实时 referencer 证据重新推导，**证据变化则拒绝执行而非静默重新计划**。
- 拒绝路径穿越与授权根外路径；`referencer_count > 0` 时明确预告将留下 redirector，redirector 清理为另行授权的独立步骤。
- 首个失败即停止，不留下后续条目未经复核的半应用计划。不提供资产删除。

### P1：已验证片段目录与 friction 回收

- 新增 `bridge_fragment_catalog` 与 `bridge_friction`（`unreal_bridge_knowledge.py`），均为本地文件加可重建索引，未新增运行时依赖。
- **verified 不是永久布尔值**：条目绑定其验证时的引擎版本、插件版本与 manifest hash；在不同环境下查询报 `stale`，存储记录不被改写，仍作为"当时验证了什么"的证据。环境字段缺失时判定失败关闭（不认定为 verified）。
- 条目文件为真，`index.json` 为派生物，可从条目重建；损坏条目上报而非静默跳过。
- friction 记录按签名去重（重复递增计数而非追加近似重复项）、设上限、写入前清除绝对用户路径；超限时丢弃最久未见者而非最高频者。

### P1：失败模式与自动测试

- 新增 `tests/test_audit_and_knowledge.py`（29 个测试），覆盖截断上报、发现分类与证据措辞、移动计划的穿越与越界拦截、陈旧计划拒绝、失败即停、目录状态随环境降级、friction 去重/上限/脱敏，以及策略提升的范围约束（其它 import 后端仍被拒、仅声明工具携带源参数、source audit 结果存在）。

## 当前证据

**构建与冷加载。** ShooterRoyal 标准 `LyraEditor Win64 Development` 构建成功；冷启动实际加载 `plugin_version 3.2.1`。46 个原生 library、1275 个函数（3.2.0 为 45/1269）；`registry_hash ebf56d9790a1ea96c630b79ef9ac93bd`，`manifest_hash 73a4c30a7d3326435e27354248e05af86d9d5bddb37049d628766c4205f9d542`，实机 ping 与磁盘 manifest 一致，`wrapper_version` 同源。Protocol 保持 2。

**离线测试。** 265 个测试通过（1 skipped）：3.2.0 基线 236 个未回归，新增 29 个全部通过。

**片段往返（实机）。** 源为 `GA_SR_ADS` 的 EventGraph，38 节点，引擎 API 报出 17 个真实外部依赖（含 `LyraGameplayAbility:SetCameraMode`、`GameplayAbility:K2_ActivateAbility`、`EnhancedInputSubsystemInterface:AddMappingContext`）。

- 导入普通 `AActor` 目标：结构接受但 `member_owners_outside_target_hierarchy` 报出 `GameplayAbility` 等 4 个 owner，实际编译 8 个错误节点——预检的诊断与实际失败吻合。
- 导入 `LyraGameplayAbility` 目标：38 节点全部导入，编译错误节点降至 2，`package_saved: false`。
- 回读签名比对显示 4 个节点结构不一致：片段经引脚调用 `Character` / `CharacterMovementComponent` / EnhancedInput 的部分在裸能力 BP 中无对应来源。**本轮不把这称为完整保真往返**；签名机制的价值正是把它暴露出来而不是让它静默通过。
- 两个独立目标分别执行，路径不是为单一样本硬编码。

**贴图导入（实机，含反例）。**

- 正例：项目内 PNG 导入 `/ShooterRoyal/_BridgeTests/T_BridgeImportTest`，`success: true`，`created_assets_for_job` 确认资产生成，`saved: false`，`side_effect_state: committed-unsaved`。ChangeSet 将本次之前已存在的两个未保存测试资产列入 `protected_preexisting_dirty_packages`，`unexpected_dirty_packages` 为空。
- 反例一（授权根外）：`C:/Windows/Web/...` 被拒，`error_code: SOURCE_PATH_NOT_AUTHORIZED`，`side_effect_state: none`。
- 反例二（路径穿越）：`docs/../../../Windows/win.ini` 归一化为 `C:/Windows/win.ini` 后按授权根拒绝——先归一化再判定包含关系，不靠字符串前缀。

**策略范围。** 重新生成后 `access_counts` 为 ReadOnly 387 / Rejected 69 / RuntimeInteraction 49 / TransactionalSync 327，相对 3.2.0 的唯一差异是贴图导入一条。`explicit_disk_save_operations` 为 4 条且 `texture|import_file` 不在其中，即 AST 审计独立确认它不执行磁盘保存。

**本轮修复的自有缺陷（均由实机验收暴露）。** 三处都是先写出来、再被真机测试否定后改正的，记录于此以免被读成一次通过：

1. `TArray<TObjectPtr<UEdGraph>>` 容器取地址类型不匹配，编译失败。
2. `Class=` 词法扫描把引号计入路径，导致每次导入都误报未解析类并阻断。
3. 误把 UE 的 `Type'Path'` 引用语法中的**类型**当作目标路径，使依赖检查恒等于 `/Script/CoreUObject.Class`，检查无意义。修正为解析内层路径后，检查才真正区分出兼容与不兼容目标。

此外 `member_owners_outside_target_hierarchy` 最初被用作 `can_import` 的闸门，实机证明它会拒绝合法片段（经引脚调用的 owner 本就不在目标层级内），已降级为诊断项。

**P1 未做实机验收。** 审计聚合、命名审计、受控移动、片段目录与 friction 回收本轮只有离线测试通过，状态为 `source_implemented` + 离线 `verified_in_scope`，**不是** 实机 `verified_in_scope`。friction 日志已记录本轮真实撞到的 3 项工具缺口（构建脚本早退仍返回 0、Build.bat 路径假设错误、策略生成器静默丢失 AST 审计）。

## 未验证与限制

- 本记录中的实机验收项另见随附的执行记录；未经真机执行的项目不得按"已支持"理解。
- 3.2.0 的既有失败保持不变，未被本轮重新标绿：远端 GAS 预测拒绝 Profile 的金币不变断言仍失败（最后一次 `0→320`，两次修复预算已耗尽）；低磁盘、部分物理写失败、外部进程错误资产视图、全部图动态绑定与跨进程校时仍未穷举。
- 片段往返覆盖 K2 事件/函数图；Material、Niagara、AnimGraph、StateTree、BehaviorTree 与 UMG WidgetTree 不在剪贴板文本覆盖范围内，需各自领域机制。
- 贴图导入的提升仅限该单一工具与该单一源参数；不构成对任何其它导入后端或任意路径读取的授权。
- 审计聚合的孤立判定与依赖计数是线索，不是完整编译或执行图。
- `UnrealBridgeUE58Library.cpp` 的匿名命名空间内仍保有一份等价 SHA-256 实现，与新增的 `UnrealBridgeSha256.h` 并存（内部链接，无 ODR 冲突）。合并为后续清理项，本轮刻意未在构建前改动该文件。
