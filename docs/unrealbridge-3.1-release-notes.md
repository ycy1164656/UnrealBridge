# UnrealBridge 3.1.0 升级说明

2026-09-10 用户授权将本次 SRUB 升级定为 3.1.0。插件描述文件的整数 Version 为 4，VersionName 为 3.1.0；原生版本、官方发现接口、MCP、manifest 与 wrapper 必须来自同一构建。Protocol 保持 2。

这是本地开发升级，尚未宣称全部交互和人工体验验收完成。3.0 的 UE 5.8.1 发布基线保留为历史；本轮限定能力在实际 UE 5.8.2-56702186 上验证。

## 能力范围

- 目标作用域、World／玩家归属、修订检查、预览和精确保存；失败保留现场，不默认 Undo。
- Montage 片段、具体 Notify／NotifyState、有限类型的 Behavior Tree／Blackboard 编辑及运行观察。
- Sound Cue 增量编辑、SoundClass／Submix／SoundMix／Control Bus 路由与自有混音会话。
- 独占 PIE 网络会话、独立 editor_game 会话、迟加入、重入、取消及所有权检查。
- Widget 局部坐标指针序列、自有窗口尺寸／DPI、捕获取消及包含 UMG 的截图。
- ShooterRoyal 的确定性样本保持项目适配层，游戏规则不进入通用插件。

支持的资产类和参数以[作用域 API 参考](../.claude/skills/unreal-bridge/references/bridge-scoped-upgrade-api.md)及实时 Schema 为准。SimpleParallel、未知 Sound Cue 节点、复杂 Submix effect chain、Launcher packaged Server 和 ARMOURY 不在已验证范围。

## 不抢占桌面

窗口尺寸接口不调用 BringToFront／ForceToFront，并拒绝自动调整最大化／最小化窗口，避免 UE 内部 Restore 再次激活窗口。指针提交要求目标窗口已经处于前台，后台返回 WindowNotActive；序列失去前台后释放自己持有的输入状态。接口不会把用户当前的工作窗口挤走，也不向操作系统注入鼠标。

用户使用电脑时暂停依赖桌面前台的指针和听感测试。经协调的后台 Editor 可以使用 UE 的 RenderOffScreen 模式做构建后检查；后台成功不能替代实际桌面指针或人工听感验收。

## 验证记录

完整逐包证据、已保存的 14 个开发样本、版本摘要与当前剩余项记录在 ShooterRoyal [278 执行记录](C:/dev/ShooterRoyal_5_8_DirectUpgrade/docs/reference/278_ShooterRoyal_UnrealBridge升级执行记录.md)。

升版本不自动把旧报告标记为新版本通过，也不替用户完成视觉、听感、手感或 Runner Progress。
