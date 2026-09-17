# Bridge client and Job reference

Read this for connection/setup or client execution details. In the maintained checkout, `scripts/` is relative to the skill directory. The Codex installation is a router: use `C:/dev/UnrealBridge/.claude/skills/unreal-bridge/scripts/bridge.py` (or a verified replacement checkout), not the router's legacy installed scripts.

## Discovery and readiness

Auto-discovery uses UDP multicast `239.255.42.99:9876`; TCP uses a per-editor port. Use `list-editors` and `--project=<name|path>` when ambiguous, and verify the actual project path before writes. `ping` success is not sufficient if `ready=false`.

On discovery failure inspect plugin presence/enabled state and whether the intended Editor is ready. These checks do not authorize installation, `.uproject` edits or relaunch. If multicast is unavailable, use a verified endpoint from the selected Editor's log via `--endpoint=host:port`.

After an authorized launch, poll readiness with a bounded host-side wait using tools actually available in the current environment. Ping before sleeping; keep the host wait shorter than its caller timeout and report meaningful changes. No GameThread sleep. Recheck readiness before execution; the rebuild helper already polls internally, but running it requires authorization to close/relaunch Editor and sync/build.

## Commands and execution

| Need | Command |
|---|---|
| Identity/liveness | `ping`, `health`, `capabilities`, `list-editors` |
| Durable work | `submit-job`, `get-job`, `wait-job`, `cancel-job`, `jobs` |
| One-shot or repeatable script | `exec --stdin`, `exec-file <path>` |
| Local script check / API lookup | `preflight <path>`, `suggest [pattern]` |
| Diagnose a blocked exec | `gamethread-ping`; use `resume` only for an identified task-owned breakpoint pause |

Use a literal PowerShell here-string or quoted shell heredoc for one-shot multi-line work; a saved script is useful when it will be rerun or retained as evidence. `--json` makes output parseable. Exit codes: 0 success, 1 runtime/transport, 2 bad arguments, 3 local AST preflight rejection. Quote shell paths; do not embed secrets in logs or examples.

For work that spans ticks, submit a short start script and `--poll-code`/`--poll-file` whose output is one JSON object containing boolean `complete`. Host-side waits do not prove native completion. Use stable idempotency keys for identical retries; inspect the returned Job and operation state before retrying uncertain calls. Increase a timeout only after identifying whether the prior operation is still running or already took effect.

## Wrappers and preflight

Generated `unreal_bridge` wrapper methods are kwargs-only. Read the selected runtime's manifest/signature for function names, arguments and return fields. Preflight checks names and arguments, not semantic types, asset existence or authorization.

- Plugin assets need a scoped plugin path or `ALL_ASSETS`; `PROJECT` covers `/Game`. Prefer bounded name/path queries rather than scanning all assets.
- Query reflected UE properties using supported accessors such as `get_editor_property`; do not assume Python attributes expose protected/EditDefaultsOnly fields. Inspect the actual return type before consuming it.
- An empty/error result warrants checking scope and parameters. Use a narrow raw `unreal.*` fallback if no matching Bridge operation exists and the project allows it; avoid broad registry walks.
- For authorized UFUNCTION/manifest changes, perform the normal build and regenerate via the checkout's `tools/gen_manifest.py`, then synchronize exact permitted source/text targets and verify readiness. Do not trigger this during a documentation-only task or bypass a mismatch.

## Grouped MCP adapter

The checkout adapter is `scripts/unreal_bridge_mcp_server.py`. Its Python dependency is `mcp>=1.6.0,<2`; preserve the upper bound because this adapter uses the pre-2.x FastMCP API. A typical launch from the verified checkout is:

```powershell
uv --directory C:/dev/UnrealBridge run --with "mcp>=1.6.0,<2" python C:/dev/UnrealBridge/.claude/skills/unreal-bridge/scripts/unreal_bridge_mcp_server.py
```

The embedded loopback MCP/REST endpoint is `http://127.0.0.1:11438`; its bearer token is at `<Project>/Saved/UnrealBridge/http-token.txt`. `GET /mcp` intentionally has no SSE. Keep tokens out of output.

Use `bridge_list_domains`, `bridge_search_tools`, `bridge_describe_tools`, `_fields`/`_omit`/`max_items`/`cursor`, and `bridge_read_artifact` to bound discovery and output. The audited 5.8.1 official Toolset baseline was 387 ReadOnly, 49 explicit-opt-in RuntimeInteraction, 326 explicit-target TransactionalSync and 70 Rejected operations; consult the live catalog for the current set. All allowed planes are no-save and exact-schema policy remains mandatory. New Python MCP tools require the host process to reload; catalog refresh alone does not reload its code.

## Encoding and persistent state

The Editor Python interpreter persists between exec calls. `print()` returns through the bridge; `unreal.log()` goes to Output Log. Use JSON and UTF-8 for captured results. If CJK text looks corrupt, compare UTF-8 bytes before changing data; matching bytes indicate a display/console encoding problem. Read files with explicit UTF-8 (or UTF-8-sig for BOM), and set subprocess output encoding where needed. Use a task-local temporary directory rather than the drive root.
