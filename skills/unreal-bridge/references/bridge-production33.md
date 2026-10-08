# UnrealBridge 3.3 内容与审计约定

维护入口为 `skills/unreal-bridge/`，Codex 路由指向此目录。`.claude/` 是停止维护的历史副本。3.3.1 保持 Protocol 2；版本号不能代替专项验收。

## 3.3.1 五项能力

- 默认 `UNREALBRIDGE_MCP_SURFACE=full`；设为 `minimal` 才缩为六个网关工具。先 `bridge_search`，用精确 capability ID `bridge_describe`，传回 schema hash、catalog fingerprint、registry hash 给 `bridge_execute`。陈旧描述必须重新发现，不绕过原有 Rejected/ChangeSet/Sandbox/target 规则。`workflow:content_import.prepare` 与 `.execute` 分开，不将任意操作字段透传给 prepare。
- `bridge_runtime_watch` 用当前 PIE session、1–4 个 World handle、精确 Actor 名和类、1–32 个字段。只读采样为 1–20 Hz、0.1–30 秒；Host 最多保持两个快照在途，时间线落 Artifact，摘要区分真实差异、恢复、原生采集间隔及 dropped samples。10 秒 / 10 Hz / 三端 / 10 字段实测 97/100，不能把请求频率当作保证频率。Actor 不唯一、World/PIE 过期或重启时失败/对账，不猜同名对象，也不调用任意函数。
- `bridge_deterministic_pie` 只接管本 Bridge 持有的 ready network PIE；Listen Server 可用 0–4 个远端 Client，Dedicated Server 至少 1 个。`begin` 保留原 pause/fixed delta；`step` 只在每个 PIE World 都严格 +N 并仍暂停时返回 exact；未知结果读 `status`，不要盲重发。`end` 精确 owner/controller 并恢复状态；若 PIE 结束，原生 EndPIE 回调恢复。该控制器不用于普通性能基线。
- `bridge_validation('assets', ...)` 需要精确对象路径和最多 256 个资产；`map_check` 只检查当前 Editor World。结果明确 missing/unable、错误/警告、限制及 Dirty；NeverSave，不把只读验证当实际游戏体验验收。
- `tools/check_mcp_context_budget.py` 用真实 MCP stdio 测 full/minimal `tools/list` 与固定 search/describe；基线在 `tests/fixtures/mcp_context_budget.json`。超门槛退出失败。完整证据与保留限制见 [3.3.1 交付记录](../../../docs/unrealbridge-3.3.1-release-notes.md)。

## Blueprint Fragment

使用当前 Editor 的 ExportFragment，再 PrepareImport。文本哈希只标识字节，不能证明依赖完整或逻辑正确。仅支持白名单 K2 event/function 节点；Material/Niagara/AnimGraph/StateTree/BT/WidgetTree 使用各自领域 API。缺成员、错误 owner/signature/graph、非本会话导出或篡改在写前拒绝。

分别核对 node_shapes_hash、internal_topology_hash、member_contract_hash，使用原生 source→target GUID 映射，不能按位置或创建顺序猜。默认对象、FText、split/container pin、内部边与 boundary ports 单独回读；外部接线属于单独声明的操作。稳定 RequestId 用于幂等；部分 intent/未知回复先对账，不能再次插入。结构或编译失败产生 SaveBlocked，ChangeSet/recipe 保存及 Sandbox Persist 均须拒绝。

目录使用 UUID+revision，同名不覆盖，索引可重建。add 仅允许 unverified/imported_unverified；promote 读取项目注册 adapter 的真实 native Job，并绑定 payload、环境、manifest、验收合同、adapter SHA、插件/依赖。绑定变化后有效状态是 stale。编译成功、保存、行为、冷加载与 human acceptance 是不同层级。

## 外部导入与重导入

`bridge_content_import`：prepare → execute → readback；get 查工单，reconcile 等原 Job。冻结 source SHA256/真实路径、精确输出、初始修订、参数、已有 Skeleton、Sandbox lease。execute 需要已有任务授权与准确 contract hash。调用超时不代表同步原生导入取消，不能新建请求重放。

readback 每次发起新只读 Job，不复用旧的读回结果。精确保存后可得到 saved_in_sandbox；离开 Sandbox 后，原 lease 的 sealed/persist_results、当前主文件 SHA1/SHA256、原生元数据与当前 Editor session 必须一致，才记录 persisted 或 cold_loaded。旧合同被后续重导入或移动替代时应明确失败，不用旧记录证明新文件。get 是历史记录，当前状态须用 readback。

默认 source 仅 Project/SourceArt 与 Project/.tmp/authorized-imports。Windows 句柄解析后的真实文件必须仍在许可根内；junction/symlink 越界、UNC/device/reserved/ADS、目录和插件源码拒绝。PNG 验证签名；FBX 检查单位、mesh/bone/take 与批准 Skeleton 的父子映射。

Profiles：显式角色 PNG、StaticMesh FBX、既有 Skeleton 的 SkeletalMesh、animation-only FBX、已有 parent/参数的 PBR MI。自动生成材质、贴图、Skeleton、PhysicsAsset 关闭。GLB/glTF 未作为已验收 Profile。MI 赋值按 getter 回读：UE 5.8 的 texture setter 可能返回 false 而已修改。repair_binding 只续接本会话旧失败合同的精确 binding_pending MI，不能接管任意 Dirty。

unexpected output、Skeleton 或保护字段变化阻止保存。重导入不是 overwrite=true：旧合同遇到属性/元数据编辑要重新计划。保留 source/选项/引用/材质/碰撞/LOD/socket/physics 证据。精确保存后记录 Sandbox hashes，preview/persist/reconcile，再正常 DLL 冷加载。UI/PBR 预览和动画实际触发、中断、死亡仍需独立验收。

PBR MI 可按新 reimport 合同更新显式列出的纹理参数，要求当前 target 干净、修订匹配、parent 不变；未列参数保留。资产身份按 Unreal 的 SoftObjectPath 比较，不能因冷加载后的包名大小写表示变化误报不同资产。冷加载若发现实际参数缺失仍必须失败，不能仅比较元数据哈希。

## 审计与整理

queued/running 必须等待同一 Job；失败、超时、畸形数据不得变成空列表。Artifact 校验 SHA/大小后完整分页，UTF-8 不截断码点。保留 Job/provider/tool/truncated/result_complete。有截断不宣称完整。

project 查 Registry、引用、类分布、包体积与已知资源指标；level 分开实例数和去重资源；content_package 查有界闭包、AssetImportData 和显式 catalog/recipe 引用。磁盘体积、资源估算、驻留内存和 GT/RT/GPU 不混用；缺指标是 unknown，空闲帧率不证明战斗性能。命名规则缺失返回 project_rule_missing，不自动采用通用前缀判错。

move plan 绑定项目/session/view、source/destination 修订、完整引用/依赖。执行前全部重查，引用数相同但身份变了也 stale。之后读回目标、源重定向器、引用解析与 Dirty；首错停止，返回已完成和未尝试项。World/OFPA、跨 mount、case-only、链式及环形普通 move 拒绝。重定向器清理独立审查，不自动全项目 fix-up。

## 真实失败模式

- 快照使用当前 package/custom versions 和非持久 archive。持久化 FText 序列化会为无 key 的 pin label 产生随机 key，使未变蓝图指纹漂移。比较逐对象摘要定位，不能关闭修订检查。
- Toolset 在零引用时可能错误迭代 None；使用明确的原生 Registry 镜像并保留 Job/失败语义，不能把异常转空。
- Blueprint 子类重建 self pin 时，可把 `object:self` 表示为声明类或目标子类。只有经原生成员引用确认的 Blueprint 自身接收者才作等价归一化；显式对象类型与成员签名仍严格比较。失败片段的原地对账要求同一可信载荷、数量/拓扑/成员一致及重新编译通过，不能重新粘贴。
- ChangeSet 的 CommitPending 仅是保存请求回执；完成 Job 后再次读取 ChangeSet 终态。编译失败实测保留未保存修改，拒绝 Save；尚有 Dirty 时 Sandbox 在进入 Persist 前拒绝，不要把该结果说成已走完文件拷贝。
- Friction 只记录真实失败、证据和可测成本；未知次数/时长为 null。区分工具/文档缺口、引擎限制、项目问题及模型误用。频率不是扩展功能的授权。损坏日志保留并报错。
- 源码、部署、DLL、manifest/wrapper、MCP host 分别核验。新增工具用 fresh host；Live Coding 后仍需正常构建与冷启动。
- 原 GAS prediction reject “金币不变”失败继续保留；在线 Provider、源码 UE、full Cook、packaged DS 等 deferred 项不自动纳入。
