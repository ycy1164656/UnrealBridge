# UnrealBridge 3.3.0 P0/P1 交付范围

本轮依据 ShooterRoyal 收尾计划及用户最新范围：完成其中 P0/P1，支持后续项目内容开发；不追求版本全功能、第三方图全面支持或发布全覆盖。Protocol 2 不变，不自动 commit/push/tag/release。

## 实施

- ReadOnly 官方 Toolset 等待同一 Job 终态，失败/超时/畸形/截断不伪装空数据；大结果完整 Artifact、UTF-8 分页边界和 SHA 校验。
- K2 Fragment 分别证明形状、内部连线、成员签名、默认值与原生 GUID 映射；可信本会话导出、幂等请求、编译/结构错误阻止保存。Blueprint 自身接收者按原生成员关系归一化，明确对象类型不放宽。
- Catalog 使用 UUID/revision 与原子持久化；verified 只能来自注册 adapter 的真实保存、Persist、行为与冷加载证据。payload、环境和依赖变化使状态失效。
- 外部路径用 Windows 真实文件句柄校验；PNG 按显式角色、StaticMesh FBX、已有 Skeleton 的 SkeletalMesh/Animation、显式 parent 与参数的 PBR MI。重导入绑定原状态、保护字段与完整依赖；意外输出阻保存。
- 导入 readback 每次是真实新 Job；记录 native file digest、原 lease 的 Persist 结果、当前主文件哈希与会话差异，区分 imported_not_saved、saved_in_sandbox、persisted、cold_loaded。历史 get 不代表当前文件仍一致。
- Move 绑定完整引用/依赖及修订，执行后核验目标、源重定向器与实际引用解析。项目、关卡、内容包审计返回覆盖范围与 unknown 指标。
- 日常工作流见 [3.3 reference](../skills/unreal-bridge/references/bridge-production33.md)。唯一维护入口 `skills/unreal-bridge`；`.claude` 不再维护。

## 验证依据与限制

消费者专项记录在 ShooterRoyal 的 `docs/331_ShooterRoyal_UnrealBridge_P0P1_实施与验证(done).md`；本轮过程证据在该项目 `.tmp/artifacts/bridge330/`，原始失败保留。正常 Build、3.3.0 冷加载、298 项离线 PASS（另 1 skip）、12 个导入/重导入工单的文件及 Persist 收据冷加载核验通过。

已完成的构建、冷加载、离线与实机结果按消费者记录列出；不把编译或截图生成成功当成完整视觉/交互验收。现有 PreviewWidget 的返回尺寸与实际 PNG 尺寸存在偏差，图标显示可核验，但该接口不能用作精确像素布局验收。

原 3.2 远端 GAS prediction reject 的“金币不变”断言失败保留为 known issue；本轮未改断言或游戏逻辑。在线 Provider、Blender/AIGC、全部第三方图、源码 UE、full Cook、packaged DS、云 DS 容量优化不属于本次收尾。
