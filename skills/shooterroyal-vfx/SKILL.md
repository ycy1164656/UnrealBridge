---
name: shooterroyal-vfx
description: 从当前ShooterRoyal全库目录自主选材、实际预览并改造技能VFX，记录配方和具体版本反馈；用于视觉创建、接入和质量修复。
---

# ShooterRoyal VFX资产优先

本技能的唯一维护源为 UnrealBridge checkout 的 `skills/shooterroyal-vfx/`；项目内的同名技能和旧脚本只作兼容入口，后续修改在本目录提交。技能迁移不迁移项目素材、目录、配方、媒体或验收记录。

遵循当前项目AGENTS。以下项目文档路径均相对当前 ShooterRoyal 根目录：工作流和艺术目标见 `docs/367_ShooterRoyal_VFX资产优先制作与全库选材执行计划.md`，事实唯一入口为 `docs/reference/data/308_ShooterRoyal_VFX全库选型目录.json`。运行Editor使用unreal-bridge。

先检查扫描时间、范围和文件修订；新增库存只补变化来源。使用本技能的`scripts/query_vfx.py`按职责、形态、运动、来源和证据筛选，默认保留未观察素材，Niagara和Cascade均参与选材。名称和家族关联是推测，实际播放后才写观察。

自主准备少量主体候选并查看生命周期。成熟资产承担主体形态、材质运动或主要生命周期；辅助层须对应明确缺口。工具失败先核对驱动和依赖，不能静默换成自制主体。原素材、进入SR未改造版、SR改造版和旧技能分别标记。

用户选定素材后，先保留其运动、拖尾、材质动画及命中/消散关系，再按具体差异改造。投射物群、范围/密度调整或本人/友军穿透问题，读[投射物动态迭代与验证](references/projectile-iteration.md)；施法动作循环与恢复由 `shooter-royal-dev` 的 `references/imported-models.md` 对应章节处理。

网格承载的VFX出现运动方向、拼接、表面贴合或背面可见性问题时，读[网格表现适配与验证](references/continuous-surface-iteration.md)。该参考只记录诊断与验证方法；具体效果的要求、实现方案、参数和认可记录留在项目配方。

## 目标效果与执行责任

Codex负责识别读取/验证缺口、说明影响并验证目标效果；不要求用户判断工具是否可靠、发现技术缺口或监督是否偷换目标。工具具备实施能力不等于每次改造已经达标。

修改前将用户要求的变化与必须保留项对应到真实资产、Emitter/材质、实际字段和驱动/覆盖链。优先用现有读取、反射、原始Export、有效值回读及实际播放查清关系；遗漏、截断、Unsupported和未确认的运行时覆盖保持未知，不猜默认值。只有关键控制关系或目标/保留项仍无法确定和验证，才判为实际阻塞；摘要少一个字段或setter返回异常本身不等于阻塞。

有阻塞时主动报告具体对象/字段、预期与实际结果、已尝试的独立读取/验证路径、未达到的效果及最小所需能力；暂停依赖该未知值的改动，继续不依赖它的已授权工作。不将工具失败自动转成另一种视觉方案。保留购买素材主体的必要局部节点、参数和绑定派生可在授权范围内完成；替换用户选定素材、另做主体/整套Shader或降低目标须有对应明确授权，缺失时先交具体缺口及可评审替代方案。

完成必须对应实际效果：原版与候选动态对照验证指定变化及保留项，再完成保存重载和本次实际涉及的整招/回收/客户端验证。验证限于本次变化及真实受影响链路，复用未变化证据，不自动重复五阶段或追加全套测试。例如“加快表面流动但保留飞行和尾迹”须分别验证表面动画、空间运动及尾迹，不能交一个自制相似效果就报完成。工程通过、Agent实际查看与用户艺术认可分别记录；编译/保存/伤害通过不能代替效果达成，机械指标也不能代替可读性、质感与节奏判断。未达到目标或关键证据不足就明确标为未完成。用户审阅具体可体验版本的艺术效果，不承担补技术验收的责任。

## 现有工具能力与条件性接口补齐

2026-10-12的[384实测](<../../../docs/384_ShooterRoyal_VFX控制能力五阶段实施与对比报告(done).md>)已验证表面流动、持续发射、命中尺寸以及独立整招：现有工具链足以支持目标明确、保留原件、可对照验证的局部魔改，没有当前必须做的工具升级或改造。直接推进具体效果，不以全库深拆、通用平台建设或工具升级作为前置；该样本不保证任意Niagara/Cascade内部机制均可读可控。后续遇到不同资产或工具版本时按本次所需范围核对真实能力。

只有任务关键关系无法读取或效果无法验证、且现有独立路径不能补齐时，才补对应接口。将补齐范围限定到一个可复现失败样本和所需字段/结果，独立复测与未知/错误负例通过后回到原效果任务；不顺势建设另一套Niagara编辑系统、全库框架或录制平台。Bridge核心、Header、Build等变更仍按AGENTS核对实际授权。

以下是已遇到症状的最小完善方向，不是待办清单或后续任务的开工条件；再次遇到时先核对当前版本和现有替代路径：

| 已遇到的具体症状 | 现有处理与需要时的最小接口范围 |
|---|---|
| Niagara组件`GetMaterial(0)`返回None，无法直接确认运行时最终材质/覆盖者 | 当前可核对实际系统、资产Renderer绑定、MI有效值及真实播放；需要完整运行时归因时才补按组件、Renderer、材质槽解析Material/MID、父链和组件/User覆盖的读取，不能解析返回unknown，不声称已直接读取GPU最终值 |
| 材质图摘要遗漏Panner速度，序列化GUID重复或通用转换丢失 | 现有反射/原始Export按表达式对象路径可消歧补值；必要时只完善现有图读取输出的对象路径、完整GUID、指定字段及读取状态，不依赖GUID唯一，也不修改作者GUID |
| MI setter返回False但值实际已写入 | 现有显式override、有效值与保存重载可判断；必要时只完善现有写入结果的实际覆盖值、有效值与verification state，不用布尔值代替成功，不据此盲目重试 |
| 长技能录制受单段20秒及旧Capture ID失效影响 | 属于证据采集便利性缺口，不影响资产控制能力；优先现有分段与真实时戳对照并披露间隔，确需补齐时仅保留每个Capture ID的独立终态、可收尾记录与真实错误原因，不扩展录制平台 |

## 保存与交付

只在声明的SR派生资产中改造，真实服务器状态驱动轨迹和命中，DS跳过表现。先在可切换、可重置的独立测试环境交付。工程、Agent视觉审查和用户艺术认可独立记录；用户反馈绑定具体配方版本、媒体和机位，不能将单体认可外推为整招或全资源包认可。

同一效果迭代时优先更新当前测试入口，并写清当前可体验版本与确认状态。已被替代的候选在授权范围内退出活跃目录，原入口/资产与替代关系记入项目配方；不要把历史对照长期作为同名或近似名称的当前入口。目录退役与删除资产是不同操作，仍遵循项目保护边界。

视觉方向未定时先交短动态对照，围绕当前反馈收敛；选定后完成相关验证和精确保存。用户说“将就”“暂时可用”“后面再调整”时记录为暂定基线，保留原话及适用版本，等新的具体反馈再改，不自动提升为最终艺术认可或继续追加候选。

## 查询与控制记录

数据字段合同、观察、媒体和配方在`docs/reference/data/vfx/`。查询示例：

```powershell
$vfxQuery = 'C:/dev/UnrealBridge/skills/shooterroyal-vfx/scripts/query_vfx.py'
$srProject = 'C:/dev/ShooterRoyal_5_8_DirectUpgrade'
python -X utf8 $vfxQuery --project-root $srProject --role projectile_body --form arrow spindle spear --element dark --limit 8
python -X utf8 $vfxQuery --project-root $srProject --pack ParagonGideon --class ParticleSystem --limit 8
python -X utf8 $vfxQuery --project-root $srProject --id VFX-898F11E3
python -X utf8 $vfxQuery --project-root $srProject --source SRAS-023 --all-family-members
python -X utf8 $vfxQuery --project-root $srProject --recipes-for SK_DPS_T_03
python -X utf8 $vfxQuery --project-root $srProject --controls-for VFX-867E4E43 --control-field SurfaceFlowRate
python -X utf8 $vfxQuery --project-root $srProject --controls-for VFX-D0FA5FCB --control-field SurfaceFlowRate
```

按实际 checkout 和项目位置调整上述路径。未传 `--project-root` 时，先读 `SHOOTERROYAL_PROJECT_ROOT`，否则从当前工作目录向上查找项目目录，不从技能所在目录推断。`merge_scan.py` 使用相同的项目定位规则；该脚本会更新项目数据，须有相应扫描合并授权，迁移验证只运行其 `--help`。

查询只读；`--export-html <输出路径>`从同一事实目录生成人类筛选表，媒体索引链接来自已记录的观察。

控制记录遵循`control-contract.json`，源/派生稳定ID和对象路径共用依据；`--reviewed`查询历史已观察，保留过期和覆盖未知的记录；`--observed`要求已查看、源/控制/外观依赖及预览配置的磁盘证据完整匹配。旧记录缺少覆盖声明时为unknown，不回填成完整。`observation_validity`分开显示各项状态；磁盘查询始终不证明当前Editor内存，`current_acceptance_eligible=false`，本次验收另查会话、Dirty、驱动和实际采集版本。只有录制不算观察，不能把今天的hash追认给旧媒体。

材质GUID重复时按表达式对象路径消歧，摘要省略值时读反射或原始Export；setter返回不能代替显式覆盖、有效值和保存重载。`scripts/control_evidence.py`提供这些窄辅助，缺失/Unsupported仍为未知。实际样本和限制见项目 `docs/384_ShooterRoyal_VFX控制能力五阶段实施与对比报告(done).md`。

新预览使用 Hub `sr_review.observation_evidence` 和 `sr_review.av_sequence` 绑定采集前依赖、实际加载 DLL、配置及同次原生声画。先从实际组件 `get_asset()` 核对播放变体，再确定依赖 roots；动态 Team2 等变体不能用预期的 Team1 路径代替。闭包截断或缺文件保持 partial，不回填旧录像。声画封装失败可用 `sequence-native.json` 与 `recover_av_sequence` 继续，不重放技能；只有新的依赖快照缺失时才需要重新采集。90秒是接口上限，实际仍受帧/字节预算限制。区间覆盖按实际帧时钟计算，技术采集、Agent查看和人工认可分开，见项目 `docs/442_ShooterRoyal_Bridge全项升级实施与实际验证(done).md`。
