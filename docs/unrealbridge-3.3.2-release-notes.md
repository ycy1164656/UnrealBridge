# UnrealBridge 3.3.2 发布记录

日期：2026-10-27。插件整数版本 8，Protocol 保持 2。本次承接已完成的视听、Niagara 与证据升级，不更改旧验证报告的历史结论。

## 功能

- 调好的 Niagara emitter 实例跨 System 移植：精确 handle/name、User 参数冲突预检、Event Handler 和 Particle Reader 显式依赖映射、编译/保存及源保持回执。
- CPU/GPU SimCache 类型化读取：精确 World/component/owner/emitter/frame，真实属性类型、有限样本、帧数/时间/容量和释放边界。
- UE 游戏混音与视频连续采集上限 90 秒；封装预算与原生预算分开，保留失败 manifest 和原始帧/音轨用于恢复。
- MetaSound 图变化比较和字面量校验、输出混音捕获与对应维护文档。

## 实际验证

UE 5.8.3 上正常构建并验证 CPU/GPU 实例、真实 Particle Reader 映射和事件映射。事件移植后实际读到 107～120 个粒子；GPU SimCache 的位置、速度、缺失属性、零粒子、owner/帧越界与容量超限已实际验证。新炮塔 35 秒、黑潮 43 秒声画可完整解码，施放/引导/消散区间覆盖完整。

3.3.2 版本常量采用正常 UBT 构建（7 actions）。隔离 HostProject commandlet 冷加载 3.3.2 后实际反射 1,295 个函数；接口、枚举和结构体与此前专项验证的实现一致，生成新的 registry/manifest/wrapper 指纹。首次空 HostProject 缺少 GameFeatureData 扫描规则，补充该测试工程的最小配置后，第二次冷读取为 0 error / 0 warning。没有修改 ShooterRoyal 原始 Config。

新 MCP stdio 进程实际回报 3.3.2。升级后必须重新启动对应 Editor/MCP 进程才能加载新代码；磁盘安装状态与旧进程当前版本分别记录。

## 限制

- SimCache 限制保留容量，不保证单次 GPU readback 的瞬时分配不超过限额；DI payload 不缓存。
- 自定义 DI 的非标准引用布局不宣称普遍支持；未知依赖不能自动视为安全。
- 技术采集、机械测试和实例播放不代表人工视觉、听感、手感或长局性能通过。
- 只保存声明的目标资产；旧场景、其他任务会话和历史证据保持。

API 入口见 [实例、GPU 读回和连续证据](../skills/unreal-bridge/references/bridge-scoped-upgrade-api.md#emitter-instances-gpu-readback-and-continuous-evidence-2026-10-27)。
