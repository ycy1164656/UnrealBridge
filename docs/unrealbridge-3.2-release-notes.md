# UnrealBridge 3.2.0 实施与验证记录

2026-09-19；Protocol 2；Windows、UE 5.8.2。版本号描述当前源码和部署，不表示所有优化目标已通过。原计划的验收标准保留。

## 已实施

- `bridge_content_recipe`：严格 typed steps、冻结计划和验收 hash、逐步 Job/原生台账、幂等、未知派发对账、最多两次修复。`verify_live` 只接受注册项目 adapter 的真实 Job 证据；调用方 JSON 的离线 verify 不成为 live-tested。
- `bridge_sandbox`：UE FileSandbox 单一活动视图与 owner/lease、真实主文件和沙箱文件指纹、精确保存收据、审查 hash、逐文件 Persist 及丢回复后的只读 `reconcile_persist`。非原子多文件提交；不承诺阻断外部写入的 CAS。
- KNOW：扩展既有 ProjectContextIndex 的中英文角色、真实字段关系、候选关系、覆盖与 stale/cursor generation；不增加平行数据库。
- `bridge_recovery`：精确进程身份、原句柄 signaled 检查、项目级 OS 锁、有限监控与至多一次重启、正常退出不重启、未知副作用停写。重启后重新发现会话，不重放业务写入。
- `bridge_capture`：自有 PIE/World 的原生帧采集、异步 JPEG 写盘、有预算的 MJPEG/AVI、逐帧解码检查、PTS/事件对应与 partial 标记；不录桌面或音轨。
- `bridge_audio_provider`：明确费用/授权合同、本地短 PCM16 WAV 验证、可注入 provider adapter、未知付费提交不再次购买。默认无在线 provider。
- 内容步骤支持 DataTable、复制资产、标量、编译/精确保存/回读、音频导入/路由、已有 Niagara 浮点参数。Niagara emitter 稳定身份与 DI/对象类型在写入前检查。
- 精确文本部署、manifest 生成的项目身份检查、带备份和冲突检查的 Codex Skill router 安装。

## 当前专项证据

ShooterRoyal 标准 `LyraEditor Win64 Development` 构建成功，冷启动实际加载 3.2.0。45 个原生 library、1269 个函数；registry `c464e184d0ce700199149450a92e30fd`，manifest `cf15342113f44ac34b8bd166584a97e2f59b2f6d4b10d8b60f8cd0c86d39c46f`。

两个兵种表、两个 SoundWave、两个 Niagara 变体已经沙箱制作、精确 Persist、冷启动重新加载。真实 AI/Spawner/波次入口的攻击、伤害与表现观测有效；注册合同的机器断言通过，人工项未代签。天赋升级/不足点数/三次同种子复测通过。PIE Listen 和 Dedicated 各两个客户端、迟加入和清理通过；它们不等于 packaged Server。

真实安全负例覆盖未知沙箱改动、主文件新版本冲突、Persist 回包丢失对账、过期 World、错误 emitter 与 DI 参数。恢复故障注入中发现“退出码已返回但进程未结束”，已经修复；新异常退出测试确认原进程结束后仅恢复一次并取得新会话。旧失败证据保留。

10 秒录像实际为 744×287、300 帧、无丢帧，已逐帧解码并查看画面。两轮无录像的 100 ms 周期性能样本：帧时间 P50 为 14.06/14.55 ms，P95 为 15.78/16.89 ms；仅作该 Editor 场景观察。

## 未通过与边界

- 远端 GAS 预测拒绝测试的金币不变断言仍失败（最后一次 0→320）；输入、拒绝和清理证据不能替代完整 Profile。两次修复预算已耗尽，不放宽断言。
- 在线音频 Provider 选型、接入与真实调用已按用户 2026-09-19 的后续指示后置（`deferred_by_user`），不再作为本轮完成阻塞项。未来启用再核对服务商、凭据及有界费用/上传条件。本地校验/导入/路由/播放验收保持当前范围，本地导入和 mock 不算在线生成。
- 音频设备有效，但后台 app/unfocused 音量乘数为 0；播放请求不等于可听波形或人工听感。视觉/听感、完整 UI 前台交互仍待体验验收。
- `declared_changeset` 在新跨 Job 内容 executor 中目前只支持计划表达，执行拒绝；已有独立 authoring 的 ChangeSet 功能保留。不得静默回退直接写主项目。
- 内容种类标签不是塔/武器/装备/英雄/UI/动画专用生成器；本轮只给这些类型记录真实角色/扩展点和明确缺口。池化特效/音频的全局无残留不由 Actor 清理结果推定。
- 未穷举低磁盘、部分物理写失败、外部进程错误资产视图、所有图动态绑定，以及跨进程校时。保留按能力/环境的验证边界。
- 源码 UE、全量 Cook、packaged Server 为计划阶段 Z，本轮未执行。

## 使用与复测

日常步骤见 [3.2 内容生产工作流](../.claude/skills/unreal-bridge/references/bridge-production32.md)。代码/安装目录存在不等于旧 MCP 进程已重载；启动新的 canonical host 后验证工具目录和版本。所有新工具沿用当前任务授权、项目规则和精确目标清单。

离线命令：`uv run --with-requirements requirements-mcp.txt --with pillow --with numpy python -m unittest discover -s tests -p 'test_*.py' -q`。运行测试前读取目标/副作用，不批量执行全部 live smoke。项目证据索引位于 ShooterRoyal 文档 327；原始结果位于其 `.tmp/artifacts/unrealbridge32/LyraEditor-Win64-Development/latest` 与 `Saved/UnrealBridge/Artifacts`。
