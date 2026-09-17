---
name: unreal-bridge
description: Inspect or automate a running Unreal Editor through UnrealBridge using the current project's live schema.
allowed-tools: Bash Read Write Edit Glob Grep Monitor
---

# UnrealBridge

Use this checkout's typed operations, durable Jobs and Python client for Unreal Editor work. The maintained workflow is Bridge 3.1 on UE 5.8; 3.0 baseline evidence used 5.8.1 and scoped 3.1 evidence used 5.8.2. Verify the live project, plugin, manifest and operation schema; this is not certification of every engine patch or API.

## Entry and scope

The CLI is this skill directory's `scripts/bridge.py`. Resolve it against the actual checkout, not a stale installed copy. Prefer the configured grouped MCP surface for supported operations. Before the first Editor operation, ping the selected project, require `ready=true`, and check relevant health/capabilities/PIE/Dirty; recheck after connection or session changes.

Follow the target project's AGENTS/authorized scope. Previously granted scope persists across turns and compaction. Report concrete targets and writes, ask only for missing authorization, and continue through the requested implementation and validation. A diagnostic request does not authorize plugin installation, `.uproject` edits, rebuild/relaunch, or asset synchronization.

## Operational invariants

- Prefer typed Bridge operations and narrow ChangeSets; raw Python setters without `Modify()` are non-transactional. A narrow `unreal.*` fallback may be used when needed and permitted by project rules, without another confirmation for the same approved scope.
- Save only declared, authorized packages and preserve unrelated Dirty. Deletion, graph-node removal, rollback and other protected actions remain subject to project prohibitions and explicit authorization; API availability is not permission.
- Respect an approved Blueprint choice. Recommend C++ only for concrete task-relevant reasons; do not require the user to insist twice. Keep affected graphs readable, compile them, and address relevant lint findings without redesigning unrelated nodes.
- Use owned runtime recipes; cleanup must match this run's session identity. Preserve a pre-existing or replacement PIE. Do not stop/kill/relaunch Editor or other processes without task authorization.
- Do not raise desktop windows during background work. Pointer input requires the exact owned PIE window already active. Preserve pending human input/audio/visual acceptance when foreground work is unavailable.
- Keep GameThread scripts short and never use blocking sleeps there. Use durable start/poll Jobs across ticks; reconcile unknown side effects after timeout/restart rather than blindly replaying them.
- Do not bypass strict schema/manifest policy. All allowed official Toolset planes are no-save, with exact tool/schema authorization; transactional calls require ChangeSet ownership. A Job submission or cancellation request is not an operation's verified terminal result.

## Task-specific references

- CLI setup/discovery, wrappers/preflight, durable Jobs, encoding and host-adapter details: [client guide](references/bridge-client-guide.md).
- World/Actor identity, project context/impact, compact graphs, owned PIE and background Trace: [reliability workflows](references/bridge-reliability-workflows.md). Synchronous Trace summary APIs are retired.
- Montage/Notify, BT, audio routing, pointer input, topology/rejoin and fixed-condition scenarios: [scoped upgrade API](references/bridge-scoped-upgrade-api.md).
- API-specific details and semantic traps: [reference index](references/bridge-reference-index.md). Read only the needed topic; player steering requires the gameplay reference because tick timing and input-axis conventions matter.

Report actual validation, saved/unsaved targets, owned-session cleanup and remaining limitations. Persisted state, source availability and old test results do not prove the current DLL, host process, gameplay or human acceptance.
