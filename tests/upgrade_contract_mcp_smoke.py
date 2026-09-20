"""Exercise the new tool through the MCP stdio protocol, against the live Editor."""
from __future__ import annotations

import argparse
import asyncio
import json
import sys
import uuid
from pathlib import Path

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


def decoded(result):
    assert not result.isError, result
    if result.structuredContent:
        return result.structuredContent.get("result", result.structuredContent)
    return json.loads(next(block.text for block in result.content if block.type == "text"))


async def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    script = root / ".claude/skills/unreal-bridge/scripts/unreal_bridge_mcp_server.py"
    parameters = StdioServerParameters(command=sys.executable, args=[str(script)], cwd=str(root))
    async with stdio_client(parameters) as (reader, writer):
        async with ClientSession(reader, writer) as session:
            initialized=await session.initialize()
            assert initialized.serverInfo.version=="3.2.0", initialized.serverInfo
            listing = await session.list_tools()
            names = {tool.name for tool in listing.tools}
            assert "bridge_submit_upgrade_validation" in names
            search = decoded(await session.call_tool("bridge_search_tools", {
                "query": "guarded", "max_items": 4, "project": args.project}))
            assert any(item.get("id") == "bridge:change_set.begin_guarded_change_set"
                       for item in search.get("result", [])), search
            envelope = decoded(await session.call_tool("bridge_call", {
                "library": "Upgrade", "function": "get_authoring_snapshot",
                "kwargs": {"target_packages_json": "[]"}, "project": args.project}))
            assert envelope["success"], envelope
            snapshot = json.loads(json.loads(envelope["output"])["result"])
            request = {key: snapshot[key] for key in
                       ("schema", "project_identity", "editor_session_id", "engine_version")}
            request.update(request_id="mcp-" + uuid.uuid4().hex, operation_id="upgrade.validate",
                           target_packages=[], expected_revisions={})
            submitted = decoded(await session.call_tool("bridge_submit_upgrade_validation", {
                "request": request, "project": args.project}))
            assert submitted["success"], submitted
            terminal = decoded(await session.call_tool("bridge_wait_job", {
                "job_id": submitted["job_id"], "wait_timeout": 15, "project": args.project}))
            assert terminal["terminal"] and terminal["job_state"] == "succeeded", terminal
            result = json.loads(terminal["job_result"]["output"])
            assert result["ok"] and result["input_digest"] == submitted["input_digest"], result
            rejected = decoded(await session.call_tool("bridge_submit_upgrade_validation", {
                "request": dict(request, save_policy="all"), "project": args.project}))
            assert rejected["error_code"] == "ScopeViolation" and not rejected["ok"], rejected
            report = {"status": "passed", "result": "SRUB_MCP_WIRE_PASS", "registered": True,
                      "mcp_version": initialized.serverInfo.version,
                      "guarded_operation_discoverable": True,
                      "positive_native_job": submitted["job_id"], "input_digest": result["input_digest"],
                      "implicit_save_rejected": True, "content_writes": 0, "tool_count": len(names)}
            Path(args.out).write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
            print(json.dumps(report), flush=True)


if __name__ == "__main__":
    asyncio.run(main())
