#!/usr/bin/env python3
"""Measure the wire-visible MCP tools/list surface and enforce frozen UTF-8 budgets."""

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
BASELINES = ROOT / "tests/fixtures/mcp_context_budget.json"
SEARCH_TERMS = ("blueprint", "niagara", "runtime", "network", "validation", "content import")


def size(value: object) -> int:
    return len(json.dumps(value, ensure_ascii=False, sort_keys=True,
                          separators=(",", ":")).encode("utf-8"))


def payload(result: object) -> dict:
    for block in result.content:
        if getattr(block, "text", None):
            return json.loads(block.text)
    raise ValueError("MCP tool returned no JSON text")


async def measure(server: Path, surface: str, endpoint: str | None = None,
                  project: str | None = None) -> dict:
    env = dict(os.environ)
    env["UNREALBRIDGE_MCP_SURFACE"] = surface
    parameters = StdioServerParameters(command=sys.executable, args=[str(server)], env=env)
    async with stdio_client(parameters) as (reader, writer):
        async with ClientSession(reader, writer) as session:
            await session.initialize()
            listing = await session.list_tools()
            tools = [tool.model_dump(mode="json", by_alias=True, exclude_none=True)
                     for tool in listing.tools]
            result = {
                "tool_count": len(tools),
                "tools_list_utf8_bytes": size({"tools": tools}),
                "total_schema_bytes": sum(size(item.get("inputSchema", {})) for item in tools),
                "total_description_bytes": sum(len(item.get("description", "").encode("utf-8")) for item in tools),
                "largest_tool_schema_bytes": max((size(item.get("inputSchema", {})) for item in tools), default=0),
                "tool_names": [item["name"] for item in tools],
            }
            if server == SERVER and surface == "minimal":
                common = {key: value for key, value in (("endpoint", endpoint),
                          ("project", project)) if value}
                search_sizes = {}
                discovered = []
                spill_count = 0
                for term in SEARCH_TERMS:
                    wire = await session.call_tool("bridge_search", {
                        "query": term, "max_items": 10, **common})
                    body = payload(wire)
                    if not body.get("success", True):
                        raise RuntimeError(f"search failed for {term}: {body}")
                    search_sizes[term] = size(wire.model_dump(mode="json", by_alias=True,
                                                               exclude_none=True))
                    spill_count += int(bool(body.get("artifact")))
                    discovered.extend(item["id"] for item in body.get("result", [])
                                      if isinstance(item, dict) and "id" in item)
                ids = list(dict.fromkeys(discovered))[:20]
                if not ids:
                    raise RuntimeError("No capability IDs discovered")
                wire = await session.call_tool("bridge_describe", {
                    "capability_ids": ids, **common})
                described = payload(wire)
                if not described.get("success", True):
                    raise RuntimeError(f"describe failed: {described}")
                spill_count += int(bool(described.get("artifact")))
                result.update({"search_utf8_bytes": search_sizes,
                               "describe_utf8_bytes": size(wire.model_dump(mode="json", by_alias=True,
                                                                             exclude_none=True)),
                               "describe_capability_count": len(ids),
                               "search_describe_artifact_spill_count": spill_count})
            return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--record", action="store_true", help="Freeze measured baselines")
    parser.add_argument("--baseline-server", type=Path,
                        help="3.3.0 server source in the same scripts directory")
    parser.add_argument("--baseline-file", type=Path, default=BASELINES)
    parser.add_argument("--endpoint", help="Optional live Editor endpoint for catalog measurement")
    parser.add_argument("--project", help="Project path bound to the live Editor")
    args = parser.parse_args()
    measured = {
        "full_331": asyncio.run(measure(SERVER, "full")),
        "minimal_331": asyncio.run(measure(SERVER, "minimal", args.endpoint, args.project)),
    }
    if args.baseline_server:
        measured["full_330"] = asyncio.run(measure(args.baseline_server.resolve(), "full"))
    if args.record:
        if "full_330" not in measured:
            parser.error("--record requires --baseline-server")
        # Freeze real observed values.  Byte tolerances are part of the policy.
        payload = {"schema": "unrealbridge.mcp_context_budget.v1", "measured": measured,
                   "minimal_byte_limit": int(measured["minimal_331"]["tools_list_utf8_bytes"] * 1.20),
                   "full_byte_limit": int(measured["full_331"]["tools_list_utf8_bytes"] * 1.10)}
        args.baseline_file.parent.mkdir(parents=True, exist_ok=True)
        args.baseline_file.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    else:
        payload = json.loads(args.baseline_file.read_text(encoding="utf-8"))
    minimal = measured["minimal_331"]
    full = measured["full_331"]
    failures = []
    if minimal["tool_count"] > 8:
        failures.append("minimal tool_count exceeds 8")
    if minimal["tools_list_utf8_bytes"] > payload["minimal_byte_limit"]:
        failures.append("minimal tools/list exceeds frozen 120% byte gate")
    if full["tools_list_utf8_bytes"] > payload["full_byte_limit"]:
        failures.append("full tools/list exceeds frozen 110% byte gate")
    frozen_search = payload["measured"]["minimal_331"].get("search_utf8_bytes", {})
    for term, measured_bytes in minimal.get("search_utf8_bytes", {}).items():
        if term in frozen_search and measured_bytes > int(frozen_search[term] * 1.20):
            failures.append(f"search {term} exceeds frozen 120% byte gate")
    frozen_describe = payload["measured"]["minimal_331"].get("describe_utf8_bytes")
    if frozen_describe and minimal.get("describe_utf8_bytes", 0) > int(frozen_describe * 1.20):
        failures.append("describe exceeds frozen 120% byte gate")
    compact = {key: {metric: value for metric, value in entry.items() if metric != "tool_names"}
               for key, entry in measured.items()}
    print(json.dumps({"measured": compact, "failures": failures}, ensure_ascii=False,
                     separators=(",", ":")))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
