#!/usr/bin/env python3
"""Export and classify the live UE 5.8 ToolsetRegistry catalog.

The output is deliberately conservative: only tools explicitly described as
read-only are allowed through the generic bridge adapter. Mutating, unknown,
and destructive tools remain discoverable but require a typed UnrealBridge
wrapper (or are rejected) before they can enter production workflows.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


REPO = Path(__file__).resolve().parents[1]
POLICY = REPO / "tools" / "ue58_toolset_policy.json"
DEFAULT_FIXTURE = REPO / "tests" / "fixtures" / "ue58-toolsets.json"
DEFAULT_MATRIX = REPO / "docs" / "ue58-toolset-capability-matrix.md"
DEFAULT_BRIDGE = (
    REPO / ".claude" / "skills" / "unreal-bridge" / "scripts" / "bridge.py"
)


def _load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def _tool_parts(toolset_name: str, raw_name: str) -> tuple[str, str]:
    prefix = f"{toolset_name}."
    if raw_name.startswith(prefix):
        return raw_name, raw_name[len(prefix) :]
    if "." in raw_name:
        return raw_name, raw_name.rsplit(".", 1)[-1]
    return f"{toolset_name}.{raw_name}", raw_name


def _normalized_name(value: str) -> str:
    return re.sub(r"[^a-z0-9]", "", value.lower())


def _hint(annotations: dict[str, Any], name: str) -> bool | None:
    value = annotations.get(name)
    return value if isinstance(value, bool) else None


def _override(
    policy: dict[str, Any], toolset_name: str, tool_name: str
) -> dict[str, Any] | None:
    for item in policy.get("overrides", []):
        if item.get("toolset") == toolset_name and item.get("tool") == tool_name:
            return item
    return None


def infer_module(toolset_name: str, policy: dict[str, Any]) -> str:
    """Infer the owning UE module from Epic's namespace-style toolset name."""
    prefix = toolset_name.split(".", 1)[0]
    return str(policy.get("module_prefix_map", {}).get(prefix, prefix))


def classify_tool(
    toolset_name: str, tool: dict[str, Any], policy: dict[str, Any]
) -> dict[str, Any]:
    raw_name = str(tool.get("name") or "")
    qualified_name, tool_name = _tool_parts(toolset_name, raw_name)
    annotations = tool.get("annotations")
    if not isinstance(annotations, dict):
        annotations = {}

    override = _override(policy, toolset_name, tool_name)
    normalized = _normalized_name(tool_name)
    destructive_by_name = any(
        fragment in normalized
        for fragment in policy.get("destructive_name_fragments", [])
    )
    destructive_hint = _hint(annotations, "destructiveHint")
    read_only_hint = _hint(annotations, "readOnlyHint")

    if override:
        bucket = "override"
        decision = dict(override)
        reason = str(decision.get("reason") or "Explicit policy override.")
        inferred_side_effects = "ReadOnlyDeclared"
    elif destructive_hint is True or destructive_by_name:
        bucket = "destructive"
        decision = dict(policy["rules"][bucket])
        reason = "Destructive annotation or destructive operation name."
        inferred_side_effects = "Destructive"
    elif read_only_hint is True:
        bucket = "read_only"
        decision = dict(policy["rules"][bucket])
        reason = "Official schema marks the tool read-only."
        inferred_side_effects = "ReadOnlyDeclared"
    elif read_only_hint is False:
        bucket = "mutating"
        decision = dict(policy["rules"][bucket])
        reason = "Official schema marks the tool non-read-only."
        inferred_side_effects = "MutatingDeclared"
    else:
        prefix_match = next(
            (
                prefix
                for prefix in policy.get("read_only_name_prefixes", [])
                if normalized.startswith(prefix)
            ),
            None,
        )
        if prefix_match:
            bucket = "unknown"
            decision = dict(policy["rules"][bucket])
            reason = (
                f"Name begins with '{prefix_match}', but the official schema has no "
                "read-only annotation; manual audit is still required."
            )
            inferred_side_effects = "ReadOnlyCandidate"
        else:
            bucket = "unknown"
            decision = dict(policy["rules"][bucket])
            reason = "Official schema does not provide enough side-effect metadata."
            inferred_side_effects = "MutatingCandidate"

    risk = str(decision.get("risk") or ("ReadOnly" if bucket == "read_only" else "Unknown"))
    if bucket == "mutating":
        risk = "Modify"
    elif bucket == "destructive":
        risk = "Destructive"

    return {
        "name": qualified_name,
        "tool": tool_name,
        "description": str(tool.get("description") or "").strip(),
        "input_schema": tool.get("inputSchema", {}),
        "output_schema": tool.get("outputSchema", {}),
        "annotations": annotations,
        "provider": policy["provider"],
        "module": infer_module(toolset_name, policy),
        "engine_min": policy["engine_min"],
        "risk": risk,
        "inferred_side_effects": inferred_side_effects,
        "execution": policy["execution"],
        "save_behavior": decision["save_behavior"],
        "classification": decision["classification"],
        "bridge_execution": decision["bridge_execution"],
        "reason": reason,
    }


def normalize_catalog(
    catalog: Any, policy: dict[str, Any], *, engine_version: str
) -> dict[str, Any]:
    if not isinstance(catalog, list):
        raise ValueError("ToolsetRegistry catalog must be a JSON array")

    toolsets: list[dict[str, Any]] = []
    for raw_toolset in catalog:
        if not isinstance(raw_toolset, dict):
            raise ValueError("Every ToolsetRegistry catalog item must be an object")
        name = str(raw_toolset.get("name") or "").strip()
        if not name:
            raise ValueError("Every ToolsetRegistry toolset must have a name")
        raw_tools = raw_toolset.get("tools", [])
        if not isinstance(raw_tools, list):
            raise ValueError(f"Toolset '{name}' has a non-array tools field")
        tools = sorted(
            (classify_tool(name, item, policy) for item in raw_tools if isinstance(item, dict)),
            key=lambda item: item["name"].casefold(),
        )
        counts = Counter(item["classification"] for item in tools)
        toolsets.append(
            {
                "name": name,
                "module": infer_module(name, policy),
                "version": str(raw_toolset.get("version") or "Unknown"),
                "description": str(raw_toolset.get("description") or "").strip(),
                "experimental": True,
                "tool_count": len(tools),
                "classification_counts": dict(sorted(counts.items())),
                "tools": tools,
            }
        )

    toolsets.sort(key=lambda item: item["name"].casefold())
    totals = Counter()
    for item in toolsets:
        totals.update(tool["classification"] for tool in item["tools"])
    return {
        "schema_version": 1,
        "generated_at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "engine_version": engine_version,
        "provider": policy["provider"],
        "engine_min": policy["engine_min"],
        "policy_file": "tools/ue58_toolset_policy.json",
        "toolset_count": len(toolsets),
        "tool_count": sum(item["tool_count"] for item in toolsets),
        "classification_counts": dict(sorted(totals.items())),
        "toolsets": toolsets,
    }


def render_markdown(audit: dict[str, Any]) -> str:
    counts = audit["classification_counts"]
    lines = [
        "# UE 5.8.1 ToolsetRegistry 能力审计",
        "",
        f"> 生成时间：`{audit['generated_at']}`",
        f"> 引擎：`{audit['engine_version']}`",
        f"> Provider：`{audit['provider']}`",
        f"> Toolset：`{audit['toolset_count']}`；Tool：`{audit['tool_count']}`",
        "",
        "通用执行策略：只有官方 schema 明确标记为只读的 `Reuse` 工具可通过通用适配器执行；"
        "`Wrap`、`Extend` 和 `Reject` 均保持阻止，直至存在经过验证的 typed wrapper。",
        "",
        "## 汇总",
        "",
        "| Reuse | Wrap | Extend | Reject |",
        "|---:|---:|---:|---:|",
        f"| {counts.get('Reuse', 0)} | {counts.get('Wrap', 0)} | {counts.get('Extend', 0)} | {counts.get('Reject', 0)} |",
        "",
        "## Toolset 明细",
        "",
        "| Toolset | Module | Version | Tools | Reuse | Wrap | Extend | Reject |",
        "|---|---|---|---:|---:|---:|---:|---:|",
    ]
    for toolset in audit["toolsets"]:
        item_counts = toolset["classification_counts"]
        lines.append(
            f"| `{toolset['name']}` | `{toolset['module']}` | `{toolset['version']}` | {toolset['tool_count']} | "
            f"{item_counts.get('Reuse', 0)} | {item_counts.get('Wrap', 0)} | "
            f"{item_counts.get('Extend', 0)} | {item_counts.get('Reject', 0)} |"
        )

    lines.extend(["", "## 工具级决策", ""])
    for toolset in audit["toolsets"]:
        lines.extend(
            [
                f"### {toolset['name']}",
                "",
                "| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |",
                "|---|---|---|---|---|---|---|",
            ]
        )
        for tool in toolset["tools"]:
            reason = tool["reason"].replace("|", "\\|").replace("\n", " ")
            lines.append(
                f"| `{tool['tool']}` | {tool['classification']} | {tool['inferred_side_effects']} | {tool['risk']} | "
                f"{tool['bridge_execution']} | {tool['save_behavior']} | {reason} |"
            )
        if not toolset["tools"]:
            lines.append("| _(none)_ | Extend | Unknown | Unknown | BlockedPendingAudit | ProviderDefined | 无工具 schema。 |")
        lines.append("")
    return "\n".join(lines).rstrip() + "\n"


def _catalog_from_editor(bridge: Path, project: str, timeout: int) -> tuple[Any, str]:
    code = (
        "import json, unreal\n"
        "catalog = unreal.UnrealBridgeUE58Library.get_official_toolset_catalog_json()\n"
        "print(json.dumps({'engine_version': unreal.SystemLibrary.get_engine_version(), "
        "'catalog': json.loads(str(catalog))}, ensure_ascii=False))\n"
    )
    command = [
        sys.executable,
        str(bridge),
        "--json",
        "--no-preflight",
        "--manifest-bootstrap",
        "--project",
        project,
        "--timeout",
        str(timeout),
        "exec",
        code,
    ]
    result = subprocess.run(
        command,
        cwd=REPO,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout + 10,
        check=False,
    )
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or result.stdout.strip() or "bridge call failed")
    outer = json.loads(result.stdout)
    if not outer.get("success"):
        raise RuntimeError(str(outer.get("error") or "bridge execution failed"))
    payload = None
    for line in reversed(str(outer.get("output") or "").splitlines()):
        try:
            payload = json.loads(line)
            break
        except json.JSONDecodeError:
            continue
    if not isinstance(payload, dict):
        raise RuntimeError("bridge output did not contain the catalog payload")
    return payload["catalog"], str(payload.get("engine_version") or "unknown")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", help="Running Editor .uproject path/name used for discovery")
    parser.add_argument("--catalog", type=Path, help="Use a previously exported raw catalog JSON")
    parser.add_argument("--engine-version", default="5.8.1", help="Engine version for --catalog mode")
    parser.add_argument("--bridge", type=Path, default=DEFAULT_BRIDGE)
    parser.add_argument("--policy", type=Path, default=POLICY)
    parser.add_argument("--fixture-out", type=Path, default=DEFAULT_FIXTURE)
    parser.add_argument("--matrix-out", type=Path, default=DEFAULT_MATRIX)
    parser.add_argument("--timeout", type=int, default=60)
    args = parser.parse_args()

    if bool(args.project) == bool(args.catalog):
        parser.error("provide exactly one of --project or --catalog")

    policy = _load_json(args.policy)
    if args.catalog:
        catalog = _load_json(args.catalog)
        engine_version = args.engine_version
    else:
        catalog, engine_version = _catalog_from_editor(args.bridge, args.project, args.timeout)

    audit = normalize_catalog(catalog, policy, engine_version=engine_version)
    args.fixture_out.parent.mkdir(parents=True, exist_ok=True)
    args.fixture_out.write_text(
        json.dumps(audit, indent=2, ensure_ascii=False, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    args.matrix_out.parent.mkdir(parents=True, exist_ok=True)
    args.matrix_out.write_text(render_markdown(audit), encoding="utf-8")
    print(
        json.dumps(
            {
                "success": True,
                "fixture": str(args.fixture_out),
                "matrix": str(args.matrix_out),
                "toolsets": audit["toolset_count"],
                "tools": audit["tool_count"],
                "classifications": audit["classification_counts"],
            },
            ensure_ascii=False,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
