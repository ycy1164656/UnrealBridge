"""Real minimal MCP stdio search/describe/execute against an identified Editor."""

from __future__ import annotations

import argparse
import asyncio
import json
import os
import sys
from pathlib import Path

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

ROOT = Path(__file__).resolve().parents[1]
SERVER = ROOT / "skills/unreal-bridge/scripts/unreal_bridge_mcp_server.py"


def payload(result):
    for block in result.content:
        if getattr(block, "text", None):
            return json.loads(block.text)
    raise ValueError("MCP tool returned no JSON text")


async def run(endpoint, project):
    env = dict(os.environ)
    env["UNREALBRIDGE_MCP_SURFACE"] = "minimal"
    params = StdioServerParameters(command=sys.executable, args=[str(SERVER)], cwd=str(ROOT), env=env)
    async with stdio_client(params) as (reader, writer):
        async with ClientSession(reader, writer) as session:
            initialized = await session.initialize()
            listing = await session.list_tools()
            names = [tool.name for tool in listing.tools]
            assert len(names) == 6 and set(names) == {"bridge_ping", "bridge_search", "bridge_describe",
                "bridge_execute", "bridge_task", "bridge_artifact"}, names
            common = {"endpoint": endpoint, "project": project}
            ping = payload(await session.call_tool("bridge_ping", common))
            assert ping.get("ready") and ping.get("plugin_version") == "3.3.2", ping
            found = payload(await session.call_tool("bridge_search", {
                "query": "is_in_pie", "provider": "native", "max_items": 10, **common}))
            record = next(item for item in found["result"]
                          if item["id"] == "native:UnrealBridgeEditorLibrary.is_in_pie")
            described = payload(await session.call_tool("bridge_describe", {
                "capability_ids": [record["id"]], **common}))
            assert described["result"]["capabilities"][0]["schema_hash"] == record["schema_hash"]
            executed = payload(await session.call_tool("bridge_execute", {
                "capability_id": record["id"], "arguments": {},
                "schema_hash": record["schema_hash"],
                "catalog_fingerprint": found["catalog_fingerprint"],
                "registry_hash": found["registry_hash"], **common}))
            assert executed.get("success"), executed
            stale = payload(await session.call_tool("bridge_execute", {
                "capability_id": record["id"], "arguments": {},
                "schema_hash": "wrong", "catalog_fingerprint": found["catalog_fingerprint"],
                "registry_hash": found["registry_hash"], **common}))
            assert stale.get("error_code") == "stale_capability", stale
            official_found = payload(await session.call_tool("bridge_search", {
                "query": "IsPIERunning", "provider": "official", "risk": "ReadOnly",
                "max_items": 10, **common}))
            official = next(item for item in official_found["result"]
                if item["id"] == "official:EditorToolset.EditorAppToolset.IsPIERunning")
            official_result = payload(await session.call_tool("bridge_execute", {
                "capability_id": official["id"], "arguments": {},
                "schema_hash": official["schema_hash"],
                "catalog_fingerprint": official_found["catalog_fingerprint"],
                "registry_hash": official_found["registry_hash"], **common}))
            assert official_result.get("success") and official_result.get("job_id"), official_result
            official_terminal = payload(await session.call_tool("bridge_task", {
                "operation": "wait", "task_kind": "job", "task_id": official_result["job_id"], **common}))
            assert official_terminal.get("success") and official_terminal.get("terminal"), official_terminal
            rejected_id = "official:animation_toolset.toolsets.controlrig.ControlRigTools.create"
            rejected_description = payload(await session.call_tool("bridge_describe", {
                "capability_ids": [rejected_id], **common}))
            rejected_schema = rejected_description["result"]["capabilities"][0]["schema_hash"]
            rejected = payload(await session.call_tool("bridge_execute", {
                "capability_id": rejected_id, "arguments": {"path": "/ShooterRoyal/Tests/NeverCreate"},
                "schema_hash": rejected_schema,
                "catalog_fingerprint": rejected_description["catalog_fingerprint"],
                "registry_hash": rejected_description["registry_hash"], **common}))
            assert rejected.get("phase") == "policy" and not rejected.get("success"), rejected
            workflows = payload(await session.call_tool("bridge_search", {
                "provider": "workflow", "max_items": 50, **common}))
            ids = {item["id"] for item in workflows["result"]}
            assert {"workflow:audit.project", "workflow:content_import.prepare",
                    "workflow:runtime.run", "workflow:runtime_watch.start",
                    "workflow:validation.assets", "workflow:validation.map_check"} <= ids, ids
            return {"server_version": initialized.serverInfo.version, "tool_count": len(names),
                    "native_read": executed.get("output"), "stale_guard": stale["error_code"],
                    "official_read_job": official_result["job_id"], "rejected_guard": rejected["phase"],
                    "workflow_count": len(ids), "plugin_version": ping["plugin_version"]}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--endpoint", required=True)
    parser.add_argument("--project", required=True)
    args = parser.parse_args()
    print(json.dumps(asyncio.run(run(args.endpoint, args.project)), ensure_ascii=False))
