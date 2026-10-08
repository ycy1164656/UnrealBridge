# UnrealBridge 3.3.1 增量交付记录

2026-09-29；基线为开发分支 `codex/ue58-unrealbridge-20260826-173148` 的 `90e78589b84e66007f234f6e6fff079c6647ebe8`。本记录描述工作区内的实现与验证，不代表已提交、推送、打标签或发布。Protocol 保持 2；`.uplugin` Version 为 7、VersionName 为 3.3.1。

## 五项能力

1. **Minimal MCP Surface**：`UNREALBRIDGE_MCP_SURFACE=minimal` 对真实 `tools/list` 仅公开 `bridge_ping/search/describe/execute/task/artifact` 六个工具。Full 模式保留既有工具。搜索包含 native、official 和闭集 host workflow；执行绑定精确 ID、schema hash、catalog fingerprint 和 host registry hash，并转交既有验证、Job、ChangeSet 与权限合同。导入 prepare/execute 是分开的 workflow ID。
2. **Multi-World Runtime Watch**：原生短 GameThread 读取绑定当前 PIE session、1–4 个精确 World handle、Actor 名/类及有界属性、GAS 数值与标签。Host 以 1–20 Hz、最多 30 秒采样；最多两个只读快照可同时在途，按序汇总原生采集时间、差异/收敛和丢样数，完整时间线保存到 Artifact。重启、超时或陈旧 handle 需要对账，不自动重启 Watch。
3. **Deterministic PIE Frame Step**：只接管 UnrealBridge 持有且已 ready 的 network PIE；Listen Server 允许 0–4 个远端 Client，Dedicated Server 仍要求至少 1 个。保存并恢复原固定时间步和 pause 状态。使用 UE 5.8 `SingleFrameAdvance`，按每个 PIE World 的实际游戏时间推进计数，在所有 World 推进后立即重暂停。每次只在所有 World 恰好推进一帧时报告 exact；未知结果须查 status，不能盲重发。
4. **Data Validation + Map Check**：原生 DataValidation 模块对精确资产路径运行限量验证；当前 Editor World 的 Map Check 只读提取 MessageLog。两者均为 NeverSave，返回缺失、上限、Dirty 和地图/选择变化信息；Map Check 失败不能等同地图有效。
5. **Context Budget Gate**：`tools/check_mcp_context_budget.py` 用真实 MCP stdio `initialize/tools/list/search/describe` 测 UTF-8 字节；冻结 3.3.0 full、3.3.1 full/minimal 基线和 120%/110% 门槛。Watch 长时间线与审计完整报告使用 Artifact，项目上下文有 `max_bytes`、truncated 和 cursor。

## 当前证据

- UE 5.8.2 的 `BuildPlugin Win64 Development` 及 ShooterRoyal `LyraEditor Win64 Development` 构建成功；增加单 World 会话后两者重新构建成功。ShooterRoyal Editor 冷加载报告插件 3.3.1、Protocol 2、registry `3e9c16c2503229411c0af54fa8de980f`、manifest `17781465b25e65bb216e4cf20980e8b93a4b27644535bf5c7b1e78ac3aa8a50c`、wrapper `17781465b25e65bb`。
- Full MCP stdio 回归通过，137 工具；minimal MCP stdio 为六工具，native 读、官方 ReadOnly Job、Rejected policy、过期 schema 拒绝和 workflow 发现通过。ShooterRoyal 项目级 `.codex/config.toml` 已将本项目的 UnrealBridge MCP 设为 minimal；新启动的真实 Codex CLI 会话经 `bridge_ping → bridge_search → bridge_describe → bridge_execute` 读回 `is_in_pie=false`。第二个真实 Codex minimal 会话在精确匹配 task-owned PIE session 后，通过同一网关生成临时 `StaticMeshActor_1`，读回位置 `(0,0,5000)`；本任务随后精确停止该 PIE，最终 Dirty=[]、PIE session 空、无资产保存。
- Listen Server + 2 Client Watch 对 `SRPlayerState_1.MatchKills` 捕获人工注入的跨端差异，随后捕获人工恢复的一致状态。5 Hz 样本 p50 109 ms、p95 204 ms 左右；差异时间线在 Artifact。该样本不证明网络复制自行收敛。
- 文档指定的 10 秒 / 10 Hz / 三端 / 10 字段 Watch 在串行调度时为 54/100 样本、46 丢样。改为最多两个有界并发只读快照后，真实 ShooterRoyal 三端复测为 **97/100 样本、3 丢样**；Job p50 110.00 ms、p95 188.00 ms，原生采集间隔 p50 100.41 ms、p95 134.09 ms。完整 606,848-byte 时间线在 `Saved/UnrealBridge/Artifacts/936d05354f5cfbcaf77c890f18596d219541c49e8c0ff148160674e298619373.json`，终态只返回摘要。10 Hz 仍有少量丢样，不保证每个时隙都采到；调用者应读取 `dropped_samples` 和原生间隔。
- 相同三端 PIE 的 `step(1)` 与 `step(10)` 均逐 World 精确 +1/+10，结束后 pause 与时间步恢复，并清理本任务持有的 PIE。此前发现 `OnWorldTickEnd` 暂停时也触发及公开单帧命令过步，两项已用实际游戏时间计数和全部 World 推进即重暂停修正。
- ShooterRoyal 的 `ASRTowerProjectile` 在本任务持有的单 World Listen PIE 中，于 1/60 秒固定步长下，`step(1)` 后前进 **50 cm**、随后 `step(10)` 前进 **500 cm**；World tick 分别精确 +1/+10，step 后仍暂停，controller 已恢复、会话 `cleaned`、Dirty=false。逐帧证据在 `.tmp/projectile331-result-C1CD72F145BA1E2DFC9194A85C181145.json`。
- Data Validation 对一个真实 ShooterRoyal DataAsset 返回 Valid；不存在的路径返回 unable/incomplete。当前 `SR_Field` Map Check 返回 0 error、0 warning，验证前后 Dirty 均为空。隔离的未保存测试地图中，先得到 0/0，再生成空 StaticMesh Actor，UE 原生 Map Check 报 **1 条 `Static mesh actor has NULL StaticMesh property` warning**，销毁该精确测试 Actor 后恢复 0/0；未保存地图或资产，证据在 `.tmp/bridge331-mapcheck-host/mapcheck331-result.json`。
- 304 个离线单测通过，1 个跳过；UE Automation 的 Catalog、World、NetworkSession 和 Upgrade NativeValidation 四项均 Success（4/4）。预算门禁的正常基线通过；人为将 minimal 上限设为 1 byte 时脚本以退出码 1 拒绝。

## Context budget

| Surface | 工具数 | `tools/list` UTF-8 bytes |
|---|---:|---:|
| 3.3.0 full | 126 | 133,797 |
| 3.3.1 full | 137 | 144,783 |
| 3.3.1 minimal | 6 | 5,871 |

Minimal 占 3.3.0 full 约 4.39%。固定六类搜索、20 ID describe 及 Artifact spill 的逐项数值见 `tests/fixtures/mcp_context_budget.json`；实现变化后必须重新实测并审查基线。项目上下文的 4,096-byte 限额样本返回 3,343 bytes、`truncated=true` 和 `next_cursor`；审计样本返回 Artifact。

## 验收边界与保留限制

- 本轮三项补验已完成：Map Check 真实 warning 负例与修复、Projectile 逐帧位移、10 Hz Watch 的实采吞吐与丢样。真实 Codex minimal 会话的只读与受控 PIE 写入也通过；结合既有 full compatibility、真实跨 World 差异、精确帧计数、Data Validation 正反例、预算 CI、P0/P1 回归及版本同源证据，**`freeze_ready=true`**（限执行计划声明的 3.3.1 范围）。
- Codex CLI 在 `approval=never` 且未单独授权 MCP 工具时会在调用前拒绝，验收会话仅临时设置 `mcp_servers.unreal_bridge.default_tools_approval_mode=approve`；项目级配置只切换 surface，不放宽审批。当前已存在的 Codex Desktop 会话仍缓存 full 工具清单，后续重新连接/启动的本项目会话才加载 minimal。
- Watch 样本的收敛来自人工恢复 client 值；服务器更新没有在该样本中覆盖已人工篡改的 client1 值，不能宣称复制自动修复。
- 首次失败 BuildPlugin 候选 `.tmp/artifacts/bridge331/plugin-win64-development/candidate` 的自动清理曾被审批拒绝；用户随后报告已手动删除。最新成功包在同级 `latest`。
- 原 GAS prediction reject/coin assertion、PreviewWidget 尺寸限制继续作为 3.3.0 已知问题；full Cook、packaged/cloud Dedicated Server、在线 Provider、Source UE 等继续 deferred。
- 不以构建、静态检查或机械通过替代声音、视觉、手感或长局性能验收。
