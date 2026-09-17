"""End-to-end smoke test for the UnrealBridge MCP STDIO entry point.

Run this script inside the dependency environment declared by
``scripts/mcp-requirements.txt``.  It starts a second Python process exactly as
Codex does, completes the MCP initialize handshake, and exercises discovery,
projection, pagination, description, artifact reads, and an optional live
Editor ping without mutating Unreal content.
"""

from __future__ import annotations

import argparse
import asyncio
import json
import sys
from pathlib import Path
from typing import Any

from mcp import ClientSession
from mcp.client.stdio import StdioServerParameters, stdio_client


REPO_ROOT = Path(__file__).resolve().parents[1]
SERVER_PATH = (
    REPO_ROOT
    / ".claude"
    / "skills"
    / "unreal-bridge"
    / "scripts"
    / "unreal_bridge_mcp_server.py"
)


def _payload(result: Any) -> dict[str, Any]:
    # FastMCP 1.x emits both the canonical JSON text block and a convenience
    # ``structuredContent={"result": ...}`` wrapper.  Parse the canonical block
    # first so the server's own top-level ``success`` and ``result`` keys are
    # preserved exactly.
    for block in getattr(result, "content", []):
        text = getattr(block, "text", None)
        if not text:
            continue
        value = json.loads(text)
        if isinstance(value, dict):
            return value
    structured = getattr(result, "structuredContent", None)
    if isinstance(structured, dict):
        wrapped = structured.get("result")
        return wrapped if isinstance(wrapped, dict) else structured
    raise AssertionError(f"MCP result did not contain a JSON object: {result!r}")


async def _run(project: str | None) -> dict[str, Any]:
    params = StdioServerParameters(
        command=sys.executable,
        args=[str(SERVER_PATH)],
        cwd=REPO_ROOT,
    )
    async with stdio_client(params) as (read_stream, write_stream):
        async with ClientSession(read_stream, write_stream) as session:
            initialized = await session.initialize()
            assert initialized.serverInfo.name == "unreal-bridge"
            assert initialized.serverInfo.version == "3.0.0"

            listed = await session.list_tools()
            tool_names = {tool.name for tool in listed.tools}
            required = {
                "bridge_list_domains",
                "bridge_search_tools",
                "bridge_describe_tools",
                "bridge_read_artifact",
                "bridge_submit_scenario",
                "bridge_submit_automation_run",
                "bridge_compare_golden_image",
                "bridge_ping",
            }
            missing = sorted(required - tool_names)
            assert not missing, f"missing MCP tools: {missing}"

            first = _payload(
                await session.call_tool(
                    "bridge_list_domains",
                    {
                        "max_items": 3,
                        "result_options": {
                            "_fields": ["domain", "tool_count"],
                        },
                    },
                )
            )
            assert first["success"] is True
            assert len(first["result"]) == 3
            assert all(set(item) <= {"domain", "tool_count"} for item in first["result"])
            cursor = first["page"]["next_cursor"]
            assert cursor

            second = _payload(
                await session.call_tool(
                    "bridge_list_domains",
                    {
                        "max_items": 3,
                        "cursor": cursor,
                        "result_options": {
                            "_fields": ["domain", "tool_count"],
                        },
                    },
                )
            )
            first_domains = {item["domain"] for item in first["result"]}
            second_domains = {item["domain"] for item in second["result"]}
            assert first_domains.isdisjoint(second_domains)

            searched = _payload(
                await session.call_tool(
                    "bridge_search_tools",
                    {
                        "query": "state tree",
                        "max_items": 2,
                        "fields": ["id", "domain", "risk"],
                    },
                )
            )
            assert searched["success"] is True and searched["result"]
            tool_id = searched["result"][0]["id"]
            described = _payload(
                await session.call_tool(
                    "bridge_describe_tools",
                    {"tool_ids": [tool_id]},
                )
            )
            assert described["success"] is True
            assert described["result"]["tools"][0]["id"] == tool_id

            operations = _payload(
                await session.call_tool(
                    "bridge_list_domain_operations",
                    {"domain": "automation", "max_items": 2},
                )
            )
            operation = operations["result"]["operations"][0]
            assert operation["operation"] == operation["id"]
            operation_description = _payload(
                await session.call_tool(
                    "bridge_describe_domain_operation",
                    {
                        "domain": "automation",
                        "operation": operation["operation"],
                    },
                )
            )
            assert operation_description["success"] is True
            assert operation_description["result"]["id"] == operation["id"]

            spilled = _payload(
                await session.call_tool(
                    "bridge_list_domains",
                    {"max_items": 1, "artifact_threshold_bytes": 1},
                )
            )
            artifact = spilled.get("artifact")
            assert artifact and artifact["artifact_id"] and artifact["sha256"]
            chunk = _payload(
                await session.call_tool(
                    "bridge_read_artifact",
                    {"artifact_id": artifact["artifact_id"], "max_bytes": 128},
                )
            )
            assert chunk["success"] is True
            assert chunk["artifact_id"] == artifact["artifact_id"]
            assert chunk["size_bytes"] == artifact["size_bytes"]

            live_ping = None
            if project:
                live_ping = _payload(
                    await session.call_tool(
                        "bridge_ping",
                        {"project": project, "timeout": 15.0},
                    )
                )
                assert live_ping.get("success") is True, live_ping

            return {
                "success": True,
                "server": {
                    "name": initialized.serverInfo.name,
                    "version": initialized.serverInfo.version,
                },
                "tool_count": len(tool_names),
                "domain_count": first["page"]["total"],
                "pagination_distinct": True,
                "described_tool": tool_id,
                "artifact": {
                    "artifact_id": artifact["artifact_id"],
                    "sha256": artifact["sha256"],
                    "size_bytes": artifact["size_bytes"],
                },
                "live_ping": live_ping,
            }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--project",
        help="Optional .uproject path used for a read-only live Editor ping.",
    )
    args = parser.parse_args()
    print(json.dumps(asyncio.run(_run(args.project)), ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
