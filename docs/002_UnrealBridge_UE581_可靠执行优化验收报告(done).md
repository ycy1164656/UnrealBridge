# UnrealBridge UE 5.8.1 可靠执行优化验收报告

日期：2026-09-05。执行依据：[六阶段计划](<plans/001_UnrealBridge_UE581_可靠执行与上下文优化执行计划(done).md>)。用户明确授权后续阶段直接执行；本轮未涉及 AnimGen、AnimDatabase、购买或安装商业服务。

## 交付结果

六阶段已按顺序实施。源码位于 `C:/dev/UnrealBridge`，原生插件文本经备份和 Hash 校验同步至 `C:/dev/ShooterRoyal_5_8_DirectUpgrade/Plugins/UnrealBridge`，使用 UE 5.8.1 正常构建并完成 Editor 重启验证。没有提交 Git，没有写入或保存 Content，没有改动 ShooterRoyal 玩法源码。

| 阶段 | 实现与结果 | 主要证据 |
|---|---|---|
| World 定位 | Session/World/Actor handle、每个 Job 片段独立作用域、歧义和跨 World 拒绝、持续输入绑定原 World；单/三 World 与原生 Automation 通过 | [World scope](../.tmp/ub-reliability/world-context-live-first.json)、[三 World guard](../.tmp/ub-reliability/world-guard-live-third.json) |
| 后台 Trace | 四类后台摘要，查询/取消/release，容量和结果预算，旧同步入口迁移错误；与旧摘要逐字段对比通过 | [Trace 实测](../.tmp/ub-reliability/trace-async-live-first.json)、[补验](../.tmp/ub-reliability/trace-post-livecoding.json) |
| 目录更新 | 按项目/endpoint/session/revision 隔离并原子刷新；执行仍要求精确 audited schema；53 toolsets、832 tools 完整匹配 | [目录实测](../.tmp/ub-reliability/catalog-live-smoke-first.json)、[原生目录 Automation](../.tmp/ub-reliability/catalog-native-automation-first.json) |
| 项目上下文 | 源码行号/Hash/符号、资产引用、Blueprint 摘要与语义候选分离；预算/敏感配置/路径边界/变更 cursor 处理 | [项目上下文](../.tmp/ub-reliability/project-context-live-first.json)、[影响查询](../.tmp/ub-reliability/project-impact-live-first.json) |
| 紧凑图 | 补齐 Pin 身份/类型/默认值，保留未知支持字段；完整往返、切片边界、diff、精确 revision dry-run 通过 | [图验收](../.tmp/ub-reliability/graph-live-verification.json) |
| 运行验收 | typed recipe、World 条件/断言/语义输入、Job 证据、独立 finally；自有 PIE nonce 防止误停替代会话；真实成功/失败/超时/取消通过 | [三端运行](../.tmp/ub-reliability/runtime-three-world-second.json)、[失败](../.tmp/ub-reliability/runtime-assertion-failure-live.json)、[超时](../.tmp/ub-reliability/runtime-condition-timeout-live.json)、[取消](../.tmp/ub-reliability/runtime-cancel-live.json) |

## 可核对的测量

- 图样本为 `/ShooterRoyal/UserInterface/Settings/WBP_SR_SettingScreen` 的 `EventGraph`，28 个节点、106 个 Pin。完整原始 JSON 122,443 bytes，紧凑 JSON 50,382 bytes，减少约 **58.9%**；原始与解码后 JSON 严格相等。未将字节变化称为 token 节省。
- 项目上下文真实定位 `SRMenuOverlayWidgetBase.cpp:53` 的设置界面引用，核对 Blueprint 父类 `/Script/ShooterRoyalRuntime.SRSettingScreenBase`。定向结果 11,058 bytes/16,384 预算；影响查询扫描 1,529 个源文件，第一页 6,647 bytes/8,192 预算，附 continuation 和过滤覆盖信息。
- Trace 样本约 33–35 MiB，性能摘要后台耗时 0.715 秒。5 次基线 GameThread ping 最大 188.144 ms，后台提交至终态期间 3 次最大 166.444 ms。这是少量本机样本，不是 p95、GB 级或长期性能结论。
- 当前语义服务配置指向外部 endpoint，已验证明确降级，本轮没有发送查询、源码或资产到该服务。现有本地结构证据仍可用。

## 文件与接口

原生修改位于 `Plugin/UnrealBridge/Source/UnrealBridge`：新增 `Public/UnrealBridgeWorldLibrary.h`、`Private/UnrealBridgeWorldLibrary.cpp`、World/Catalog 原生测试；调整 Gameplay、Networking、Server、Module、Perf、UE58 与 Blueprint 对应实现及必要声明。各次 exact-sync 收据在 `.tmp/codex-backups/`，不使用会复制 Content 的全插件同步。

host 修改位于 `.claude/skills/unreal-bridge/scripts`：新增 `unreal_bridge_catalog.py`、`unreal_bridge_project_context.py`、`unreal_bridge_graph_codec.py`、`unreal_bridge_runtime.py`；修改 MCP server、ScenarioManager、manifest/preflight 缓存。新增与更新的定向 tests、原生 tests 和 live smoke 脚本保留在仓库。

已更新生成的 manifest/wrapper 元数据、仓库 `.claude`/`.codex` 技能入口、本机 `C:/Users/ycy/.codex/skills/unreal-bridge/SKILL.md` 和 Trace/可靠执行参考。具体调用契约与示例见 [可靠执行接口参考](../.claude/skills/unreal-bridge/references/bridge-reliability-workflows.md)。

主要新增 host 入口：`bridge_project_context`、`bridge_project_impact`、`bridge_graph_export/decode/diff/dry_run`、`bridge_runtime_run/status/cancel`。World scope 在 `bridge_submit_job(world_handle=...)` 使用；Trace 通过 `Perf.start_trace_analysis` 及状态/结果/取消接口调用。

## 构建与验证

- [最终 host 回归](../.tmp/ub-reliability/final-host-regression.json)：**98 tests、0 failures、0 errors、0 skipped**。包括 World 11、Trace host 2、Catalog 20、Context 12、Graph 12、Runtime 21、Domain 8、Workflow 12。
- [最终正常构建](../.tmp/ub-reliability/final-full-build-1.log)：`LyraEditor Win64 Development`，5 actions、11.34 秒、Succeeded。早期 Header/目录阶段正常构建及原生 Automation 的证据仍在恢复指针中。
- [重启后集成验证](../.tmp/ub-reliability/final-cold-integration.json)：旧 World handle 拒绝、目录 session 变化、完整图内容不变、自建单 World PIE 与清理通过。原生模块冷启动后的 BeginPIE/EndPIE 路径已运行。
- 三份技能入口均通过 quick_validate；[Git whitespace 检查](../.tmp/ub-reliability/final-whitespace-check.json)通过，仅有既有 CRLF 转换提示。[15 个目标安装文件](../.tmp/ub-reliability/final-installed-files.json)与源文件 Hash 一致。
- SRTR Build/Check 通过，sourceDigest `dcd5b6d1ad917840624b2e178db58cab1d211c26fd746602055614658f139fee`；1 tutorial、6 packages、6 steps。保留 6 项缺少可选 `estimated_minutes` 的 Warning，不编造时长。Runner Test 全部通过，见 [日志](../.tmp/ub-reliability/srtr/runner-tests-final.log)，未发布 Runner。
- [文档审计](../.tmp/ub-reliability/document-audit-final.json)：13 份文档、2 个连续编号、11 个既有无编号文档；无重号、断号、断链、重复 Marker ID、过时计划引用或错误状态后缀。

## 限制与失败记录

- 目录刷新不会把未经审计的新工具自动变成可执行。当前审计权限仍为 387 ReadOnly、49 RuntimeInteraction、326 TransactionalSync、70 Rejected。
- 上下文是有边界的词法和资产证据，不是完整 C++ 调用图；语义结果不证明依赖。外部语义服务、真实 embedding/caption 生成未验证。
- 图往返限于快照支持字段；不承诺 `.uasset` 完整序列化。dry-run 不写图、不删节点；ApplyGraphOps 仍非原子，正式写入另需原生 schema 验证及精确资产范围。
- Trace 的 alloc/net/cook 流程通过，但样本没有对应事件，保留明确 warnings；取消为协作式，不能保证所有 Provider 遍历具有硬耗时上界。
- Runtime 断线、Editor 重启、用户替换 PIE 和未知 dispatch 的异常状态用隔离测试验证；未向真实用户 Editor 注入断线/崩溃。真实网络验收证明运行与声明断言，不代表战斗、复制正确性、画面或手感全部验收。
- 实施中修复过首次 World 编译错误、错误 API/测试字段断言及 Windows 报告替换暂时被占用。失败证据保留，后续修复和通过记录优先；不把失败重写成成功。
- 新增 MCP Python 工具已用 fresh host 验证。现有客户端若仍持有旧常驻 adapter，需要重载该 adapter 才会发现新工具；本轮未声称客户端旧进程热更新。当前没有可调用的 MCP 进程重载工具。

## 最终状态与下一指针

Editor PID **49036**，地图 `/ShooterRoyal/Maps/SR_Field`。最终 **Dirty content=[]、Dirty maps=[]、PIE=false、Shader jobs=0、Asset compile jobs=0、runtime lease=null**。无资产保存，无残留本任务 PIE；没有执行 MapCheck，因为没有编辑或保存地图，不能声称 MapCheck 已运行。

Bridge `ready=true`、3.0.0/protocol 2、40 libraries/1228 functions；registry `89a91833ed30a038b1c7338c68501061`，manifest `47d8fe6a2dcfc452480a2684c1cf6a6771ab30d6ac0eaed8977cc9873f93816e`。

六阶段无需继续领取工作包。后续使用时从 [技能接口参考](../.claude/skills/unreal-bridge/references/bridge-reliability-workflows.md) 选择对应工具；恢复和审计从 [最终执行指针](../.tmp/codex-handoffs/ub-reliability-20260905.md) 进入。原始用户改动保留，未暂存、未提交、未回滚。
