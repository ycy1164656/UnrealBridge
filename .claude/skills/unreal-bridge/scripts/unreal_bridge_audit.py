#!/usr/bin/env python3
"""Read-only project/level audit aggregation and the controlled organize layer.

Like unreal_bridge_workflows, this module imports neither MCP nor Unreal: the
caller injects a transport callable, which keeps classification, planning and
the move/dry-run contract unit-testable without a running editor.

Two deliberate boundaries:

* Audit is read-only.  It orchestrates measurement capability that already
  exists (AssetTools.find_assets / get_dependencies / get_referencers /
  get_asset_tags, all ReadOnly in the generated policy) and never optimizes,
  deletes, bakes or moves anything.
* Organize separates planning from execution.  A plan is produced from
  read-only evidence and is the only thing execution will act on; a plan whose
  evidence moved underneath it is refused rather than re-derived, because a
  silent re-derivation would move assets the caller never reviewed.
"""

from __future__ import annotations

import hashlib
import json
import re
from dataclasses import dataclass, field
from typing import Any, Callable, Dict, Iterable, List, Optional, Sequence, Tuple


# Tool ids are spelled once so a toolset rename surfaces here, not in six places.
T_FIND_ASSETS = ("editor_toolset.toolsets.asset.AssetTools", "find_assets")
T_GET_DEPENDENCIES = ("editor_toolset.toolsets.asset.AssetTools", "get_dependencies")
T_GET_REFERENCERS = ("editor_toolset.toolsets.asset.AssetTools", "get_referencers")
T_GET_ASSET_TAGS = ("editor_toolset.toolsets.asset.AssetTools", "get_asset_tags")
T_GET_ASSET_CLASS = ("editor_toolset.toolsets.asset.AssetTools", "get_asset_class")
T_MOVE = ("editor_toolset.toolsets.asset.AssetTools", "move")

# G-03 finding categories. Kept closed so a caller cannot invent a severity that
# downstream reporting does not understand.
CATEGORY_GAMEPLAY_FAULT = "gameplay_fault"
CATEGORY_PERFORMANCE_LEAD = "performance_lead"
CATEGORY_ORGANIZATION = "organization_suggestion"
CATEGORIES = (CATEGORY_GAMEPLAY_FAULT, CATEGORY_PERFORMANCE_LEAD, CATEGORY_ORGANIZATION)

SEVERITY_ORDER = {"high": 0, "medium": 1, "low": 2}

DEFAULT_MAX_ASSETS = 2000

# Prefix conventions are project policy, not engine truth, so they are data.
DEFAULT_NAMING_RULES: Dict[str, str] = {
    "Blueprint": "BP_",
    "WidgetBlueprint": "WBP_",
    "AnimBlueprint": "ABP_",
    "Material": "M_",
    "MaterialInstanceConstant": "MI_",
    "Texture2D": "T_",
    "StaticMesh": "SM_",
    "SkeletalMesh": "SK_",
    "DataTable": "DT_",
    "SoundWave": "S_",
    "NiagaraSystem": "NS_",
}


class AuditError(RuntimeError):
    """Raised when audit or organize cannot produce a trustworthy result."""


def _digest(value: Any) -> str:
    return hashlib.sha256(
        json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), default=str)
        .encode("utf-8")
    ).hexdigest()


def _as_list(value: Any) -> List[Any]:
    if value is None:
        return []
    if isinstance(value, list):
        return value
    if isinstance(value, dict):
        # Tool results sometimes wrap the payload; accept the common shapes
        # rather than guessing a single one.
        for key in ("result", "results", "assets", "items", "value"):
            if key in value:
                return _as_list(value[key])
        return [value]
    return [value]


@dataclass
class Finding:
    """One audited observation, always carrying the evidence it came from."""

    category: str
    severity: str
    summary: str
    asset: str
    evidence: Dict[str, Any] = field(default_factory=dict)

    def __post_init__(self) -> None:
        if self.category not in CATEGORIES:
            raise AuditError(f"Unknown finding category: {self.category}")
        if self.severity not in SEVERITY_ORDER:
            raise AuditError(f"Unknown severity: {self.severity}")

    def to_dict(self) -> Dict[str, Any]:
        return {
            "category": self.category,
            "severity": self.severity,
            "summary": self.summary,
            "asset": self.asset,
            "evidence": self.evidence,
        }


def _call_readonly(call: Callable[..., Any], tool: Tuple[str, str], arguments: Dict[str, Any]) -> Any:
    toolset, name = tool
    response = call(toolset=toolset, tool=name, arguments=arguments)
    if isinstance(response, dict) and response.get("success") is False:
        raise AuditError(f"{toolset}.{name} failed: {response.get('error') or response}")
    return response


def collect_assets(
    call: Callable[..., Any],
    scope_paths: Sequence[str],
    *,
    max_assets: int = DEFAULT_MAX_ASSETS,
) -> Tuple[List[str], bool]:
    """Enumerate assets under the given content paths.

    Returns (assets, truncated). Truncation is reported, never hidden: a
    partial sweep that looks complete would make "no findings" meaningless.
    """
    if not scope_paths:
        raise AuditError("scope_paths must name at least one content path")
    seen: List[str] = []
    known: set[str] = set()
    truncated = False
    for path in scope_paths:
        if not str(path).startswith("/"):
            raise AuditError(f"scope path must be a /Game-style content path: {path}")
        found = _as_list(_call_readonly(call, T_FIND_ASSETS, {"path": path, "recursive": True}))
        for item in found:
            asset = item if isinstance(item, str) else (item or {}).get("path") or (item or {}).get("asset")
            if not asset or asset in known:
                continue
            if len(seen) >= max_assets:
                truncated = True
                break
            known.add(asset)
            seen.append(asset)
        if truncated:
            break
    return seen, truncated


def audit_project(
    call: Callable[..., Any],
    scope_paths: Sequence[str],
    *,
    max_assets: int = DEFAULT_MAX_ASSETS,
    naming_rules: Optional[Dict[str, str]] = None,
) -> Dict[str, Any]:
    """Aggregate read-only project findings.

    Only orchestration: every number here comes from an existing ReadOnly tool.
    Nothing is modified, and no optimization is applied.
    """
    rules = dict(DEFAULT_NAMING_RULES if naming_rules is None else naming_rules)
    assets, truncated = collect_assets(call, scope_paths, max_assets=max_assets)

    findings: List[Finding] = []
    orphan_count = 0
    heavy_dependency_count = 0

    for asset in assets:
        referencers = _as_list(_call_readonly(call, T_GET_REFERENCERS, {"asset_path": asset}))
        dependencies = _as_list(_call_readonly(call, T_GET_DEPENDENCIES, {"asset_path": asset}))

        if not referencers:
            orphan_count += 1
            findings.append(Finding(
                category=CATEGORY_ORGANIZATION,
                severity="low",
                summary="Asset has no referencers in the audited scope",
                asset=asset,
                evidence={
                    "referencer_count": 0,
                    "scope_paths": list(scope_paths),
                    "basis": "AssetTools.get_referencers over the audited scope only; "
                             "an asset referenced from outside the scope, from code, or by "
                             "soft path can still appear unreferenced here",
                },
            ))

        if len(dependencies) >= 50:
            heavy_dependency_count += 1
            findings.append(Finding(
                category=CATEGORY_PERFORMANCE_LEAD,
                severity="medium" if len(dependencies) < 150 else "high",
                summary=f"Asset pulls {len(dependencies)} dependencies",
                asset=asset,
                evidence={
                    "dependency_count": len(dependencies),
                    "basis": "AssetTools.get_dependencies; a lead to verify with a real "
                             "measurement, not a confirmed cost",
                },
            ))

    findings.extend(_naming_findings(call, assets, rules))

    findings.sort(key=lambda f: (SEVERITY_ORDER[f.severity], f.category, f.asset))
    return {
        "ok": True,
        "scope_paths": list(scope_paths),
        "asset_count": len(assets),
        "truncated": truncated,
        "max_assets": max_assets,
        "counts": {
            "total": len(findings),
            "by_category": {
                category: sum(1 for f in findings if f.category == category)
                for category in CATEGORIES
            },
            "orphan_candidates": orphan_count,
            "heavy_dependency_assets": heavy_dependency_count,
        },
        "findings": [f.to_dict() for f in findings],
        "mutations": "none; this aggregation is read-only by construction",
    }


def _asset_leaf(asset_path: str) -> str:
    leaf = asset_path.rsplit("/", 1)[-1]
    return leaf.split(".", 1)[0]


def _naming_findings(
    call: Callable[..., Any],
    assets: Sequence[str],
    rules: Dict[str, str],
) -> List[Finding]:
    findings: List[Finding] = []
    for asset in assets:
        asset_class = _call_readonly(call, T_GET_ASSET_CLASS, {"asset_path": asset})
        if isinstance(asset_class, dict):
            asset_class = asset_class.get("result") or asset_class.get("class") or ""
        asset_class = str(asset_class or "").rsplit("/", 1)[-1].split(".", 1)[-1]
        expected = rules.get(asset_class)
        if not expected:
            continue
        leaf = _asset_leaf(asset)
        if not leaf.startswith(expected):
            findings.append(Finding(
                category=CATEGORY_ORGANIZATION,
                severity="low",
                summary=f"{asset_class} '{leaf}' does not use the '{expected}' prefix",
                asset=asset,
                evidence={
                    "asset_class": asset_class,
                    "expected_prefix": expected,
                    "basis": "project naming convention supplied as data, not an engine rule",
                },
            ))
    return findings


def naming_audit(
    call: Callable[..., Any],
    scope_paths: Sequence[str],
    *,
    naming_rules: Optional[Dict[str, str]] = None,
    max_assets: int = DEFAULT_MAX_ASSETS,
) -> Dict[str, Any]:
    """Read-only naming audit. E-01 requires this before any move is planned."""
    rules = dict(DEFAULT_NAMING_RULES if naming_rules is None else naming_rules)
    assets, truncated = collect_assets(call, scope_paths, max_assets=max_assets)
    findings = _naming_findings(call, assets, rules)
    return {
        "ok": True,
        "scope_paths": list(scope_paths),
        "asset_count": len(assets),
        "truncated": truncated,
        "violation_count": len(findings),
        "violations": [f.to_dict() for f in findings],
        "mutations": "none",
    }


def plan_moves(
    call: Callable[..., Any],
    moves: Sequence[Dict[str, str]],
    *,
    allow_roots: Sequence[str] = ("/Game",),
) -> Dict[str, Any]:
    """Produce a reviewable move plan from read-only evidence (E-02).

    The returned plan carries a digest of the exact (source, destination,
    referencer-count) tuples it was built from. apply_moves refuses a plan
    whose digest no longer matches the live evidence.
    """
    if not moves:
        raise AuditError("moves must contain at least one {source, destination} entry")

    entries: List[Dict[str, Any]] = []
    blocked: List[Dict[str, Any]] = []
    for move in moves:
        source = str(move.get("source") or "").strip()
        destination = str(move.get("destination") or "").strip()
        if not source or not destination:
            raise AuditError("every move requires an explicit source and destination")
        reason = _move_block_reason(source, destination, allow_roots)
        if reason:
            blocked.append({"source": source, "destination": destination, "reason": reason})
            continue
        referencers = _as_list(_call_readonly(call, T_GET_REFERENCERS, {"asset_path": source}))
        entries.append({
            "source": source,
            "destination": destination,
            "referencer_count": len(referencers),
            "redirector_expected": len(referencers) > 0,
        })

    plan = {
        "entries": entries,
        "allow_roots": list(allow_roots),
    }
    return {
        "ok": len(blocked) == 0,
        "plan": plan,
        "plan_digest": _digest(plan),
        "move_count": len(entries),
        "blocked": blocked,
        "mutations": "none; this is a plan, not an execution",
        "note": "Applying this plan leaves redirectors wherever referencer_count > 0. "
                "Redirector cleanup is a separate, separately-authorized step.",
    }


def _move_block_reason(source: str, destination: str, allow_roots: Sequence[str]) -> Optional[str]:
    if not source.startswith("/") or not destination.startswith("/"):
        return "both source and destination must be /Game-style content paths"
    if source == destination:
        return "source and destination are identical"
    for path in (source, destination):
        if not any(path == root or path.startswith(root.rstrip("/") + "/") for root in allow_roots):
            return f"path '{path}' is outside the authorized roots {list(allow_roots)}"
        if ".." in path:
            return f"path '{path}' contains a traversal segment"
    return None


def apply_moves(
    call: Callable[..., Any],
    plan: Dict[str, Any],
    plan_digest: str,
    *,
    confirm: bool = False,
) -> Dict[str, Any]:
    """Execute a previously produced move plan (E-03).

    Requires the caller to pass back both the plan and its digest, and to set
    confirm=True. The digest is re-derived from live referencer counts: if the
    evidence changed, the plan is refused rather than silently re-planned.
    """
    if not confirm:
        raise AuditError("apply_moves requires confirm=True; a plan is not an authorization")
    entries = list((plan or {}).get("entries") or [])
    if not entries:
        raise AuditError("plan contains no entries")

    revalidated = {
        "entries": [],
        "allow_roots": list((plan or {}).get("allow_roots") or ["/Game"]),
    }
    for entry in entries:
        referencers = _as_list(_call_readonly(call, T_GET_REFERENCERS, {"asset_path": entry["source"]}))
        revalidated["entries"].append({
            "source": entry["source"],
            "destination": entry["destination"],
            "referencer_count": len(referencers),
            "redirector_expected": len(referencers) > 0,
        })
    live_digest = _digest(revalidated)
    if live_digest != plan_digest:
        return {
            "ok": False,
            "error": "plan_stale",
            "detail": "Referencer evidence changed since the plan was reviewed; "
                      "re-run plan_moves and review the new plan.",
            "expected_digest": plan_digest,
            "live_digest": live_digest,
            "moved": [],
        }

    moved: List[Dict[str, Any]] = []
    failed: List[Dict[str, Any]] = []
    for entry in revalidated["entries"]:
        try:
            _call_readonly(call, T_MOVE, {
                "source_path": entry["source"],
                "destination_path": entry["destination"],
            })
        except AuditError as exc:
            # Stop at the first failure: continuing would leave a half-applied
            # plan whose remaining entries were never re-validated.
            failed.append({"source": entry["source"], "error": str(exc)})
            break
        moved.append(entry)

    return {
        "ok": not failed,
        "moved": moved,
        "moved_count": len(moved),
        "failed": failed,
        "remaining": len(revalidated["entries"]) - len(moved) - len(failed),
        "packages_saved": False,
        "note": "Moves are transactional in the editor but unsaved; saving belongs "
                "to the caller's ChangeSet. Redirectors remain where referencers existed.",
    }
