# UnrealBridge 3.4.0 发布记录

日期：2026-11-10。按用户要求由 3.3.2 提升一个 minor 为 3.4.0；插件整数版本 9，Protocol 保持 2。本版收录前轮已实施补丁，不改写历史报告的结论。

## 已收录内容

- Niagara 精确 emitter 修订前置条件：比较 graph、renderer、event、DI、rapid-iteration 五类指纹，身份更换或旧计划在事务开始前拒绝。
- 单模块输入修改识别唯一模块 alias；模块不存在或有歧义时拒绝，避免插入同脚本模块后按旧序号修改错误对象。
- 官方事务批次在 apply 失败／异步拒绝出口保留 `calls`、`completed_call_count`、`failure_phase` 与既有 ChangeSet／side-effect 回执。
- VFX 查询识别明确的历史观看状态；将带逐帧时间定位的 observed tags 与推断标签及其来源分开，历史观看不代替当前依赖修订或人工认可。
- 同步描述符、原生版本常量、MCP 版本、当前测试断言及中英文 README。

既有 emitter 实例移植、限量 GPU SimCache、连续游戏声画能力沿用 [3.3.2 记录](unrealbridge-3.3.2-release-notes.md)。Autopilot 的 ShooterRoyal 适配器仍位于项目 `Tools/BridgeAutopilot`，本版没有将其宣称为插件内置的任意游戏自治模块。

## 本次验证

- 使用 UE `5.8.2-0+UE5` 的任务自有 HostProject，正常 UBT `-Module=UnrealBridge -NoEngineChanges` 编译、链接完成；最终增量 4 actions、17.29 秒。没有覆盖 ShooterRoyal 工程插件或启动游戏 PIE。
- 独立 `UnrealEditor-Cmd` 冷启动实际读取 3.4.0／Protocol 2、50 个类、1,295 个函数。原生进程退出码 0；manifest、meta 和 kwargs-only wrapper 从该次实际反射生成，未手工修改版本字段伪装运行结果。
- UE58 契约 7、Niagara 修订保护 5、Niagara v3 契约 9、域注册 8、导入顺序 5、VFX 项目路径 4，共 38 项测试通过。
- VFX 查询 19 项：18 通过，1 项因原始历史扫描备份不可用而跳过；真实 R6 的历史观看记录召回已通过，未将历史记录升级为当前艺术认可。
- 全新 MCP stdio 进程实际回报 3.4.0，137 项工具；发现、分页、描述及任务专用 artifact 根的写入／分块读回通过。不是原桌面宿主被原地热更新。
- 原生 registry hash：`340b350126fa8afe2dd5aa4e50ba59b5`；manifest SHA256：`818964193fcd8a1609d6c0e5e7839fa6c0d7cd39a9a362aa95b9df71e7efb709`。

轻量验证回执位于 `.tmp/artifacts/minor-release/UnrealEditor-Win64-Development/latest/` 的 `coldread-summary.json`、`contract-results.json` 和原始日志。

## 验证中的失败与修正

- 初次新 Host 全目标构建展开为 4,699 项引擎动作，按精确进程身份停止本任务构建，改为仅构建 UnrealBridge 模块。其后一次日志延迟刷新导致链接被过早中断；刷新自有暂存 Header 时间后，最终模块编译和完整链接成功。中断记录保留，不计作成功构建。
- 模块单独构建不生成插件发现清单；隔离 Host 的 `.modules` 使用实际目标引擎 BuildId 和本轮构建 DLL 生成。随后由独立原生冷启动确认可以加载，不把清单写入视为兼容性验证本身。
- 初次冷启动遇到当前引擎默认启用的 ResonanceAudio 缺少模块。只在自有 Host 设置 `DisableEnginePluginsByDefault=true`，保留 UnrealBridge 及其显式依赖；没有修改 Engine 或 ShooterRoyal 原始配置。
- commandlet 的脚本打印位于 `HostProject/Saved/Logs/HostProject.log`，标准输出只含 Display。初次外层解析未找到标记后，改为从本次原生日志解析实际 JSON，保留原生退出码与项目身份检查。
- 最终冷启动 0 error、4 warning，均为隔离 Host 关闭默认插件后缺少 `/ACLPlugin/ACLAnimBoneCompressionSettings`／`ACLAnimCurveCompressionSettings`；未覆盖动画压缩或项目运行验证。

## 限制

- 本次不扩大 UE 支持合同，不重跑历史全部功能矩阵或人工视听验收。
- 旧 MCP／Editor 进程不会因版本文件更新热加载；工程安装、运行时版本和仓库发布分别记录。
- VFX 工作流由本仓库维护；未另建独立 VFXBridge 或 REAPERBridge 仓库。
- 默认 Windows AppContainer artifact 目录首次冒烟出现 `artifact path escaped store`。显式设置任务专用 `UNREALBRIDGE_ARTIFACT_ROOT` 后通过；本版未追加默认目录的修复或普遍可用性承诺。
