---
name: unreal-bridge
description: Inspect or automate a running Unreal Editor through UnrealBridge using the current project's live schema.
---

# UnrealBridge for Codex

This is the Bridge 3.2 documentation router. The canonical checkout is `{{CHECKOUT}}`.
Use `{{SKILL}}/scripts/bridge.py` and its adjacent manifest, or the configured grouped MCP tools. Do not use an installed legacy `scripts/` snapshot. Missing canonical files are a dependency to resolve, never a reason to silently fall back.

Before the first Editor operation, ping the exact project, require `ready=true`, and inspect health/capabilities, PIE and Dirty. Verify loaded identity again after reconnect or relaunch. Source version, deployed files, loaded DLL, host code and manifest are separate evidence. A host must reload to expose new tools. Pure documentation work needs no Editor.

Read the maintained [workflow]({{SKILL}}/SKILL.md), then only the reference required by the task:

- [Connection, strict schema and Jobs]({{SKILL}}/references/bridge-client-guide.md).
- [World identity, project evidence, owned PIE and recovery]({{SKILL}}/references/bridge-reliability-workflows.md).
- [Scoped authoring, audio, input and topology]({{SKILL}}/references/bridge-scoped-upgrade-api.md).
- [3.2 content recipes, fixed verification, sandbox, recovery and evidence]({{SKILL}}/references/bridge-production32.md).
- [API reference index]({{SKILL}}/references/bridge-reference-index.md).

Reuse the project's task authorization; do not ask again for the same scope. Declare exact targets and save only task-owned packages. Prefer narrow typed ChangeSets; never bypass schema checks, delete protected assets/Graph nodes, perform unapproved rollback, or save unrelated Dirty work. End only the task's matching owned sessions. Do not activate desktop windows; pointer input requires the owned window already active. Never sleep on the Editor GameThread.

Complete the authorized task through validation and precise saves. Report implemented, offline-tested, live-tested and human-accepted separately. Unknown dispatched side effects require reconciliation before retries. Stage Z/source-engine/full Server builds remain deferred unless specifically authorized. Online audio requires an identified provider and a bounded request authorization; offline fixtures are not online generation.
