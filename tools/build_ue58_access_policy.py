#!/usr/bin/env python3
"""Build the exact UE 5.8 official-tool execution policy and compact catalog.

The generated policy is deny-by-default.  Every executable entry is tied to
an exact toolset/tool id and a hash of its structural input schema.  Runtime
interaction and transactional asset mutation are separate execution planes;
filesystem/path mutations and destructive tools remain rejected.
"""

from __future__ import annotations

import argparse
import ast
import hashlib
import json
from collections import Counter
from pathlib import Path
from typing import Any


REPO = Path(__file__).resolve().parents[1]
DEFAULT_AUDIT = REPO / "tests" / "fixtures" / "ue58-toolsets.json"
DEFAULT_POLICY = (
    REPO
    / "Plugin"
    / "UnrealBridge"
    / "Resources"
    / "ue58_official_tool_policy.json"
)
DEFAULT_CATALOG = (
    REPO
    / ".claude"
    / "skills"
    / "unreal-bridge"
    / "scripts"
    / "official_tool_catalog.json"
)


DOCUMENTATION_SCHEMA_KEYS = {
    "description",
    "title",
    "examples",
    "$comment",
}

EXTERNAL_OR_PATH_MUTATION_FRAGMENTS = (
    "export",
    "import",
    "reimport",
    "save",
    "checkout",
    "checkin",
    "submit",
    "sourcecontrol",
    "source_control",
    "renameasset",
    "rename_asset",
    "moveasset",
    "move_asset",
    "migrate",
)

RUNTIME_TOOLSETS = {
    "AutomationTestToolset.AutomationTestToolset",
    "GameFeaturesToolset.GameFeaturesToolset",
    "SlateInspectorToolset.SlateInspectorToolset",
}

RUNTIME_NAME_FRAGMENTS = (
    "activategamefeature",
    "deactivategamefeature",
    "executecue",
    "executegraphinstance",
    "runpcginstantgraph",
    "spawngraphinstance",
    "startpie",
    "stoppie",
)

SEQUENCER_RUNTIME_NAMES = {
    "close_curve_editor",
    "close_sequence",
    "curve_editor_empty_selection",
    "curve_editor_select_keys",
    "empty_selection",
    "focus_parent_sequence",
    "focus_sub_sequence",
    "force_evaluate",
    "frame_selection",
    "hide_all_controls",
    "open_curve_editor",
    "open_sequence",
    "pause",
    "play",
    "play_to",
    "refresh_sequence",
    "select_bindings",
    "select_channels",
    "select_control",
    "select_folders",
    "select_mirrored_controls",
    "select_sections",
    "select_tracks",
    "set_outliner_selection",
    "show_all_controls",
}

SLATE_READ_ONLY = {"ListObservers", "Snapshot", "Windows"}
AUTOMATION_READ_ONLY = {"GetTestResults", "GetTestStatus", "ListTests"}
GAME_FEATURE_READ_ONLY = {
    "GetGameFeatureState",
    "IsGameFeatureActive",
    "IsGameFeaturePlugin",
    "ListDiscoveredGameFeaturePlugins",
    "ListEnabledGameFeaturePlugins",
}

# A few official Toolsets implement inspection through functions which the
# upstream audit conservatively labels ``MutatingCandidate``.  These exact
# operations were source-audited in UE 5.8.1 and do not change UObject state.
# Keep this list exact (toolset + tool), rather than trusting name prefixes, so
# a newly added similarly named operation stays denied until it is reviewed.
EXACT_READ_ONLY_OPERATIONS = {
    *(f"SlateInspectorToolset.SlateInspectorToolset|{name}" for name in SLATE_READ_ONLY),
    *(f"AutomationTestToolset.AutomationTestToolset|{name}" for name in AUTOMATION_READ_ONLY),
    *(f"GameFeaturesToolset.GameFeaturesToolset|{name}" for name in GAME_FEATURE_READ_ONLY),
    "NiagaraToolsets.NiagaraToolset_Info|UEnum_Info",
    "animation_toolset.toolsets.import_export.SequencerImportExportTools|get_linked_anim_sequences",
    "editor_toolset.toolsets.asset.AssetTools|exists",
    "editor_toolset.toolsets.asset.AssetTools|load_asset",
    "editor_toolset.toolsets.programmatic.ProgrammaticToolset|get_execution_environment",
}

# These UE 5.8.1 operations were verified against their implementation rather
# than inferred from their public name.  Keep the exact keys here as a safe
# fallback when the policy is regenerated on a machine without Engine sources.
EXACT_REJECTED_OPERATIONS = {
    "animation_toolset.toolsets.controlrig.ControlRigTools|create": (
        "UE 5.8.1 implementation calls EditorAssetLibrary.save_asset and cannot "
        "participate in a no-save preview ChangeSet."
    ),
    "editor_toolset.toolsets.scene.SceneTools|commit_level_instance": (
        "UE 5.8.1 implementation can call save_dirty_packages and commit level "
        "instance changes to disk."
    ),
    "editor_toolset.toolsets.programmatic.ProgrammaticToolset|execute_tool_script": (
        "Arbitrary provider-side Python execution is outside the typed official adapter."
    ),
}

EXACT_RUNTIME_OPERATIONS = {
    "editor_toolset.toolsets.scene.SceneTools|edit_level_instance": (
        "Enters persistent Editor level-instance edit mode; explicit runtime-side-effect opt-in is required."
    ),
}

# These names contain import/export/save terminology but mutate only declared
# Unreal assets; they do not read or write arbitrary external paths.  They are
# safe only on the TransactionalSync plane with explicit target packages.
EXACT_INTERNAL_ASSET_MUTATIONS = {
    "animation_toolset.toolsets.controlrig.ControlRigTools|import_bones_from_asset",
    "animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools|save_default_spawnable_state",
    "animation_toolset.toolsets.import_export.SequencerImportExportTools|export_anim_sequence",
}

DISK_SAVE_CALL_TERMINALS = {
    "save_actor",
    "save_asset",
    "save_assets",
    "save_dirty_packages",
    "save_dirty_packages_with_dialog",
    "save_loaded_asset",
    "save_loaded_assets",
    "save_map",
    "save_package",
    "save_packages",
    "save_world",
}


def _attribute_name(node: ast.AST) -> str:
    parts: list[str] = []
    current = node
    while isinstance(current, ast.Attribute):
        parts.append(current.attr)
        current = current.value
    if isinstance(current, ast.Name):
        parts.append(current.id)
    return ".".join(reversed(parts))


def _has_tool_call_decorator(function: ast.FunctionDef | ast.AsyncFunctionDef) -> bool:
    for decorator in function.decorator_list:
        target = decorator.func if isinstance(decorator, ast.Call) else decorator
        if _attribute_name(target).split(".")[-1:] == ["tool_call"]:
            return True
    return False


def _python_module_for_toolset_source(path: Path) -> str | None:
    parts = path.parts
    for index in range(len(parts) - 1):
        if parts[index].lower() == "content" and parts[index + 1].lower() == "python":
            relative = Path(*parts[index + 2 :]).with_suffix("")
            return ".".join(relative.parts)
    return None


def audit_python_tool_sources(toolsets_root: Path) -> dict[str, list[str]]:
    """Return decorated official tools which contain an explicit disk-save call.

    The audit is intentionally syntax based and conservative.  It does not try
    to execute Engine Python or infer whether a conditional save branch will be
    taken: the presence of a disk-save API makes the generic no-save adapter
    unsuitable and forces use of a dedicated typed workflow (or rejection).
    """

    findings: dict[str, set[str]] = {}
    for path in sorted(toolsets_root.rglob("*.py")):
        if "tests" in {part.lower() for part in path.parts}:
            continue
        module = _python_module_for_toolset_source(path)
        if not module:
            continue
        try:
            tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
        except (OSError, SyntaxError, UnicodeDecodeError):
            continue
        for class_node in (node for node in tree.body if isinstance(node, ast.ClassDef)):
            for function in (
                node
                for node in class_node.body
                if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
                and _has_tool_call_decorator(node)
            ):
                save_calls = {
                    call_name
                    for call in ast.walk(function)
                    if isinstance(call, ast.Call)
                    and (call_name := _attribute_name(call.func))
                    and call_name.split(".")[-1].lower() in DISK_SAVE_CALL_TERMINALS
                }
                if save_calls:
                    operation_id = f"{module}.{class_node.name}|{function.name}"
                    findings.setdefault(operation_id, set()).update(save_calls)
    return {key: sorted(values) for key, values in sorted(findings.items())}


def _compact_schema(value: Any) -> Any:
    if isinstance(value, dict):
        return {
            key: _compact_schema(value[key])
            for key in sorted(value)
            if key not in DOCUMENTATION_SCHEMA_KEYS
        }
    if isinstance(value, list):
        return [_compact_schema(item) for item in value]
    return value


def _schema_hash(schema: Any) -> str:
    compact = _compact_schema(schema)
    encoded = json.dumps(
        compact,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def _normalized(value: str) -> str:
    return "".join(character.lower() for character in value if character.isalnum())


def _is_runtime(toolset: dict[str, Any], tool: dict[str, Any]) -> bool:
    toolset_name = str(toolset["name"])
    name = str(tool["tool"])
    normalized = _normalized(name)
    if toolset_name == "AutomationTestToolset.AutomationTestToolset":
        return name not in AUTOMATION_READ_ONLY
    if toolset_name == "GameFeaturesToolset.GameFeaturesToolset":
        return name not in GAME_FEATURE_READ_ONLY
    if toolset_name == "SlateInspectorToolset.SlateInspectorToolset":
        return name not in SLATE_READ_ONLY
    if name in SEQUENCER_RUNTIME_NAMES:
        return True
    return any(fragment in normalized for fragment in RUNTIME_NAME_FRAGMENTS)


def _is_external_or_path_mutation(tool: dict[str, Any]) -> bool:
    # Descriptions often contain harmless words such as "unsaved" or explain
    # how a later operation may save.  Classify the operation symbol itself;
    # implementation-level saves are handled by the AST source audit above.
    normalized = _normalized(str(tool.get("tool", "")))
    return any(_normalized(fragment) in normalized for fragment in EXTERNAL_OR_PATH_MUTATION_FRAGMENTS)


def _access_for(
    toolset: dict[str, Any],
    tool: dict[str, Any],
    source_save_findings: dict[str, list[str]] | None = None,
) -> tuple[str, str]:
    side_effects = str(tool.get("inferred_side_effects") or "")
    operation_id = f"{toolset['name']}|{tool['tool']}"
    if tool.get("classification") == "Reject" or side_effects == "Destructive":
        return "Rejected", "Destructive tools are never exposed by the generic official adapter."
    if operation_id in EXACT_REJECTED_OPERATIONS:
        return "Rejected", EXACT_REJECTED_OPERATIONS[operation_id]
    if source_save_findings and operation_id in source_save_findings:
        calls = ", ".join(source_save_findings[operation_id])
        return "Rejected", f"Source audit found explicit disk-save call(s): {calls}."
    if operation_id in EXACT_READ_ONLY_OPERATIONS:
        return "ReadOnly", "Exact UE 5.8.1 implementation was source-audited as inspection-only."
    if operation_id in EXACT_RUNTIME_OPERATIONS:
        return "RuntimeInteraction", EXACT_RUNTIME_OPERATIONS[operation_id]
    if (
        operation_id not in EXACT_INTERNAL_ASSET_MUTATIONS
        and _is_external_or_path_mutation(tool)
    ):
        return "Rejected", "External file/source-control or asset-path mutations require a dedicated typed workflow."
    if _is_runtime(toolset, tool):
        return "RuntimeInteraction", "Audited runtime/editor interaction; caller must opt in to runtime side effects."
    if side_effects in {"ReadOnlyDeclared", "ReadOnlyCandidate"}:
        return "ReadOnly", "Audited query with no asset-save or destructive behavior."
    return "TransactionalSync", "Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required."


def build_policy(
    audit: dict[str, Any],
    source_save_findings: dict[str, list[str]] | None = None,
) -> tuple[dict[str, Any], dict[str, Any]]:
    entries: dict[str, Any] = {}
    compact_toolsets: list[dict[str, Any]] = []
    counts: Counter[str] = Counter()
    for toolset in audit["toolsets"]:
        compact_tools: list[dict[str, Any]] = []
        for tool in toolset["tools"]:
            access, reason = _access_for(toolset, tool, source_save_findings)
            key = f"{toolset['name']}|{tool['tool']}"
            schema_hash = _schema_hash(tool.get("input_schema", {}))
            entry = {
                "toolset": toolset["name"],
                "tool": tool["tool"],
                "module": toolset["module"],
                "access": access,
                "risk": tool.get("risk", "Unknown"),
                "schema_sha256": schema_hash,
                "save_behavior": "Never" if access != "Rejected" else "Rejected",
                "requires_explicit_targets": access == "TransactionalSync",
                "requires_immediate_completion": access == "TransactionalSync",
                "requires_runtime_opt_in": access == "RuntimeInteraction",
                "reason": reason,
            }
            entries[key] = entry
            counts[access] += 1
            compact_tools.append(
                {
                    "name": tool["name"],
                    "tool": tool["tool"],
                    "description": tool.get("description", ""),
                    "provider": tool.get("provider", "EpicToolsetRegistry"),
                    "risk": tool.get("risk", "Unknown"),
                    "classification": tool.get("classification"),
                    "bridge_execution": access,
                    "save_behavior": entry["save_behavior"],
                    "input_schema": tool.get("input_schema", {}),
                    "output_schema": tool.get("output_schema", {}),
                    "schema_sha256": schema_hash,
                }
            )
        compact_toolsets.append(
            {
                "name": toolset["name"],
                "module": toolset["module"],
                "version": toolset.get("version", "Unknown"),
                "description": toolset.get("description", ""),
                "tool_count": len(compact_tools),
                "tools": compact_tools,
            }
        )

    policy = {
        "schema_version": 1,
        "engine_min": audit.get("engine_min", "5.8.0"),
        "engine_version_audited": audit.get("engine_version", "5.8.1"),
        "provider": "EpicToolsetRegistry",
        "deny_by_default": True,
        "tool_count": len(entries),
        "access_counts": dict(sorted(counts.items())),
        "source_audit": {
            "explicit_disk_save_operations": sorted((source_save_findings or {}).keys()),
            "explicit_disk_save_operation_count": len(source_save_findings or {}),
        },
        "tools": entries,
    }
    compact_catalog = {
        "schema_version": 1,
        "engine_min": audit.get("engine_min", "5.8.0"),
        "engine_version": audit.get("engine_version", "5.8.1"),
        "provider": "EpicToolsetRegistry",
        "toolset_count": len(compact_toolsets),
        "tool_count": len(entries),
        "access_counts": dict(sorted(counts.items())),
        "source_audit": policy["source_audit"],
        "toolsets": compact_toolsets,
    }
    return policy, compact_catalog


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--audit", type=Path, default=DEFAULT_AUDIT)
    parser.add_argument("--policy-out", type=Path, default=DEFAULT_POLICY)
    parser.add_argument("--catalog-out", type=Path, default=DEFAULT_CATALOG)
    parser.add_argument(
        "--toolsets-source-root",
        type=Path,
        help="Optional UE Engine/Plugins/Experimental/Toolsets root for AST disk-save auditing.",
    )
    args = parser.parse_args()

    audit = json.loads(args.audit.read_text(encoding="utf-8"))
    source_save_findings = (
        audit_python_tool_sources(args.toolsets_source_root)
        if args.toolsets_source_root
        else {}
    )
    policy, catalog = build_policy(audit, source_save_findings)
    args.policy_out.parent.mkdir(parents=True, exist_ok=True)
    args.catalog_out.parent.mkdir(parents=True, exist_ok=True)
    args.policy_out.write_text(
        json.dumps(policy, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    args.catalog_out.write_text(
        json.dumps(catalog, ensure_ascii=False, separators=(",", ":")) + "\n",
        encoding="utf-8",
    )
    print(
        json.dumps(
            {
                "success": True,
                "policy": str(args.policy_out),
                "catalog": str(args.catalog_out),
                "tools": policy["tool_count"],
                "access": policy["access_counts"],
            },
            ensure_ascii=False,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
