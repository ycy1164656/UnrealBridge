---
name: shooterroyal-vfx
description: 从当前ShooterRoyal全库目录自主选材、实际预览并改造技能VFX，记录配方和具体版本反馈；用于视觉创建、接入和质量修复。
---

# ShooterRoyal VFX 技能入口

阅读并遵循 [UnrealBridge 中维护的完整技能]({{SKILL}}/SKILL.md)。正文、参考与脚本的唯一维护源为 `{{SKILL}}`；后续修改在 UnrealBridge 仓库提交，本入口不保存第二套实现。

项目素材、308 目录、配方、媒体与验收记录仍由当前 ShooterRoyal 工程管理。调用 `{{SKILL}}/scripts/query_vfx.py` 时明确传入 `--project-root <当前项目根目录>`，或遵循完整技能的项目定位规则。
