# UnrealBridge 3.2：受约束的内容生产

本页描述已实现的入口，不等于对任意项目、所有内容类型或人工体验作保证。版本、部署副本、当前 DLL、host 与 manifest 分开核对。3.2 继续使用 Protocol 2；使用 `mcp>=1.6,<2`。

## 一张工单的最小输入

给出目标、成熟模板、允许差异、精确输出 Package、验收 Profile、预算和授权范围。先调用 `bridge_project_context(resolve_roles=true, include_relations=true)`；项目 `Tools/UnrealBridge/project_roles.json` 仅选择候选，真实关系以 AssetRegistry、DataTable 字段或源码行证据为准。中文别名不构成已接入证明。索引报告包含覆盖范围、视图和 generation；cursor 失效后重新读取。

1. 查询当前 project/session/PIE/Dirty；按任务授权声明完整写集合。
2. `bridge_sandbox` 创建当前工单独占的沙箱，取得实际 lease。已有沙箱不能接管。不可用时报告该项阻塞，不静默改为直接写主项目。
3. 读取模板与每个目标的 `Upgrade.get_authoring_snapshot`，冻结 `bridge_content_recipe(operation="plan")`。冻结哈希覆盖步骤和独立验收合同。
4. `execute_step` 每次派发一个已冻结 typed step；`reconcile_step` 按实际 Job ID 取终态。保存只影响声明的目标。未知派发不得重放。
5. 原生确认完全未改动的属性步骤可用 `reconcile_unchanged` 取得新尝试资格，保留前次错误，最多两次。编译后派生对象导致修订变化时，必须独立核查精确差异；`accept_reviewed_revision` 是显式审查动作，不是自动放宽冲突。
6. 显式运行已注册的项目 Scenario/输入或 AI 夹具。使用 `verify_live` 读取注册 adapter 的真实 Job 观测；调用方提供 JSON 的 `verify` 只作离线诊断，永不成为 live-tested。
7. `record_saved`、`preview_persist`、携带新 review hash 的 `persist_approved` 依次执行。未知变更、主文件冲突和旧 hash 均拒绝。丢回复使用 `reconcile_persist` 逐文件读回，不重新购买/重放写入。
8. `leave_preserving` 保留沙箱文件；主视图核验与冷加载另记。保存不等于 Persist，Persist 不等于视觉/听感验收。

## 实际支持与边界

内容 executor 支持 DataTable 建表/复制行/标量字段、资产复制、普通 UObject 标量、编译/精确保存/回读、短 PCM16 WAV 导入/路由和已有 Niagara 浮点参数。浮点子配方核对全部 emitter 稳定 ID；禁止隐式替换 DI、对象或动态绑定。Blueprint 默认值、Graph、任意动画重定向不通过通用标量操作冒充实现。

`declared_changeset` 可表达计划，但这个跨 Job 内容 executor 尚不能安全接续完整原生 ChangeSet，执行时拒绝；已有独立 typed authoring API 的 ChangeSet 能力仍可用。内容种类标签不是领域生成器：塔/武器/装备/英雄/天赋/UI 复用项目数据库和现有 adapter，具体变体必须有各自合同及实际验证。

项目注册表 `Tools/UnrealBridge/verification_adapters.json` 指向项目内有界模块和内容种类；collector 核对模块 SHA、实际观察器加载 SHA、World 句柄、session、保存与主文件 SHA1。重新启动或 Persist 后可核验原合同与当前视图的连续性；不能沿用旧 Actor/World handle。目录属于可信项目代码，检索/日志/Provider 文本只是数据。

## 证据、恢复和音频

- `bridge_capture` 需要任务拥有的 PIE、明确 World 和录制授权；最多 20 秒、30 fps、1280×720、256 MiB。真实视口 JPEG 与 host MJPEG/AVI、逐帧解码、PTS 映射、丢帧和事件误差分别记录。没有音轨和桌面录制；录制可能改变帧时间，性能基线另跑。
- `bridge_recovery` 默认不监控。授权注册后使用进程外、单工单锁、精确 PID/创建时间/路径与有限 lease，最多重启一次。只有原进程句柄已 signaled 且退出码异常才进入重启；有退出码但句柄未结束是 `termination_pending`，不能并开 Editor。正常退出、调试器暂停、身份复用、未知写入不会被当作可自动重试的崩溃。恢复后重新发现 schema/视图，不自动写资产。
- `bridge_audio_provider` 默认 unconfigured，支持本地 PCM 校验、预算合同和可注入 Provider adapter。真实在线生成需要实际 Provider、凭据、已知费用上限和上传/付费授权；mock/local 不算在线生成。未知 submit 不再次购买。原文件不暗改；听感保持 human pending。
- 人工判断与机器 required/optional 分开。事件缺失是 inconclusive；直接写最终健康值不算真实输入；跨进程只凭因果 ID 关联，未经校时不相减 monotonic 时间。
- 不自动启用源码 UE、全量 Cook 或 packaged Server。原生 PIE/listen/editor_game 能力按实际专项结果报告。

## 部署与复测

`tools/deploy_scoped.py --project <exact.uproject> <exact plugin-relative files>` 默认预览；`--apply` 只部署白名单文本，核对旧 hash 并备份，不同步 Content 资产。`tools/gen_manifest.py --project <exact.uproject> --deploy` 在身份匹配后生成并部署 manifest/wrapper。

`tools/install_codex_skill.py` 默认预览；授权后 `--apply` 安装指向 canonical checkout 的路由。定制旧路由需先审阅，并以 `--replace-customized` 备份替换。旧 scripts 保留但不用。已运行的 MCP host 需重新加载才能增加工具；可先用 canonical CLI/新 host 验证，不将源码存在等同于已加载。

离线回归：`uv run --with-requirements requirements-mcp.txt --with pillow --with numpy python -m unittest discover -s tests -p 'test_*.py' -q`。正式 DLL 构建与真实 Editor 验证独立记录，不用单元测试代替。
