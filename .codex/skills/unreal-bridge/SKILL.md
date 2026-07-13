---
name: unreal-bridge
description: Use UnrealBridge from Codex to inspect or automate a running Unreal Editor through its local TCP or HTTP MCP endpoints.
---

# UnrealBridge for Codex

Use this skill when working with a running Unreal Editor that has the
`UnrealBridge` plugin installed and enabled.

## Bridge Path

Default local checkout:

```powershell
$Bridge = "C:\dev\UnrealBridge\.claude\skills\unreal-bridge\scripts\bridge.py"
```

If the repo is somewhere else, set `$Bridge` to that checkout's
`.claude\skills\unreal-bridge\scripts\bridge.py`.

## First Check

Always ping before using the editor:

```powershell
python $Bridge ping
python $Bridge health
python $Bridge capabilities
```

If multiple editors are running:

```powershell
python $Bridge list-editors
python $Bridge --project "ShooterRoyal" ping
```

## Execute Code

Prefer a PowerShell here-string for one-shot multi-line work:

```powershell
@'
from unreal_bridge import Asset, Level
paths, _ = Asset.search_assets_in_all_content(query="Hero", max_results=10)
for path in paths:
    print(path.export_text() if hasattr(path, "export_text") else str(path))
'@ | python $Bridge exec --stdin
```

Use JSON output when the result needs to be parsed:

```powershell
@'
import json
from unreal_bridge import Editor
print(json.dumps({"state": Editor.get_editor_state().export_text()}, ensure_ascii=False))
'@ | python $Bridge --json exec --stdin
```

## API Contract

Prefer the generated wrapper module:

```python
from unreal_bridge import Asset, Level, Blueprint, Editor, GameplayAbility, GameplayTag
```

Wrapper methods are kwargs-only. Avoid positional arguments. After adding or
renaming any `UFUNCTION` in the plugin, rebuild the editor plugin and regenerate
the manifest/wrapper:

```powershell
python C:\dev\UnrealBridge\tools\gen_manifest.py --timeout 120
```

Do not bypass a manifest mismatch. Rebuild, regenerate, synchronize the plugin,
and require `ping` to report `ready=true`.

## Durable Jobs

Use `submit-job` when the caller must not lose the result after a client timeout
or disconnect:

```powershell
python $Bridge --project ShooterRoyal --json --idempotency-key audit-001 submit-job "print('audit')"
python $Bridge --project ShooterRoyal --json get-job <job_id>
python $Bridge --project ShooterRoyal --json wait-job <job_id> --wait-timeout 30
```

For work that must span Editor ticks, submit a short start script plus a short
poll script with `--poll-code` or `--poll-file`. The poll script must print one
JSON object containing a boolean `complete` field. Never emulate this with
`time.sleep()` inside UE Python.

## Embedded HTTP MCP

Protocol v2 also exposes a loopback-only MCP/REST endpoint at
`http://127.0.0.1:11438`. Its bearer token is stored in
`<Project>/Saved/UnrealBridge/http-token.txt`. Prefer Job submission and polling
for tool calls; `GET /mcp` intentionally does not provide SSE.

## Safety

- Do not delete assets, actors, files, or Blueprint nodes without explicit user confirmation.
- Before state-changing editor scripts, describe the target assets and operations.
- Use bridge APIs over raw `unreal.*` when a matching bridge API exists.
- For raw fallback scripts, keep them narrow and inspect first.
- Do not use `time.sleep()` inside bridge-executed scripts that need the GameThread to tick.
- Use ChangeSet preview/commit/rollback for multi-object writes and save only packages owned by the Job.
- Treat raw Python setters that do not call `Modify()` as non-transactional; prefer Bridge mutators.

## MCP Adapter

Codex can also connect through the optional grouped MCP adapter:

```powershell
python C:\dev\UnrealBridge\.claude\skills\unreal-bridge\scripts\unreal_bridge_mcp_server.py
```

The adapter exposes grouped tools (`bridge_call`, `asset_op`, `level_op`,
`blueprint_op`, `umg_op`, etc.) instead of one tool per `UFUNCTION`.
