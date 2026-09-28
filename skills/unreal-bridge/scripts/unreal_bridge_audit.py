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
CATEGORIES += ('stability_network', 'unknown_coverage')

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

    def __init__(self, message, evidence=None):
        super().__init__(message)
        self.evidence = evidence or {}


def _digest(value: Any) -> str:
    return hashlib.sha256(
        json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), default=str)
        .encode("utf-8")
    ).hexdigest()


def _as_list(value: Any) -> List[Any]:
    if value is None:
        raise AuditError("Missing collection result; empty evidence must be an explicit array")
    if isinstance(value, list):
        return value
    if isinstance(value, dict):
        # Tool results sometimes wrap the payload; accept the common shapes
        # rather than guessing a single one.
        for key in ("returnValue", "result", "results", "assets", "items", "value"):
            if key in value:
                return _as_list(value[key])
        raise AuditError("Unrecognized collection result", value)
    raise AuditError("Expected an explicit collection, got " + type(value).__name__)


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
            "confidence": "observed_lead" if self.category != CATEGORY_GAMEPLAY_FAULT else "requires_reproduction",
            "evidence_kind": "editor_registry_observation",
            "recommended_next_measurement": "Inspect references and runtime traces in the affected scenario before changing assets",
        }


def _call_readonly(call: Callable[..., Any], tool: Tuple[str, str], arguments: Dict[str, Any]) -> Any:
    toolset, name = tool
    response = call(toolset=toolset, tool=name, arguments=arguments)
    if isinstance(response, dict) and (response.get("success") is False or response.get("result_complete") is False):
        raise AuditError(f"{toolset}.{name} failed: {response.get('error') or response}", response)
    if isinstance(response, dict) and response.get("result_complete") is True:
        return response["result"]
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
    if not 1 <= max_assets <= 10000:
        raise AuditError("max_assets must be between 1 and 10000")
    seen: List[str] = []
    known: set[str] = set()
    truncated = False
    for path in scope_paths:
        if not str(path).startswith("/"):
            raise AuditError(f"scope path must be a /Game-style content path: {path}")
        found = _as_list(_call_readonly(call, T_FIND_ASSETS, {"folder_path": path, "name": "", "recursive": True}))
        for item in found:
            asset = item if isinstance(item, str) else (item or {}).get("path") or (item or {}).get("asset")
            if not asset:
                raise AuditError("Asset enumeration contained an unrecognized record", {"record": item})
            if asset in known:
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
    rules = dict(naming_rules or {})
    assets, truncated = collect_assets(call, scope_paths, max_assets=max_assets)

    findings: List[Finding] = []
    orphan_count = 0
    heavy_dependency_count = 0
    rows = []

    for asset in assets:
        referencers = _as_list(_call_readonly(call, T_GET_REFERENCERS, {"asset_path": asset}))
        dependencies = _as_list(_call_readonly(call, T_GET_DEPENDENCIES, {"asset_path": asset}))
        asset_class = _call_readonly(call, T_GET_ASSET_CLASS, {'asset_path': asset})
        tags = _call_readonly(call, T_GET_ASSET_TAGS, {'asset_path': asset})
        rows.append({'asset': asset, 'class': asset_class, 'tags': tags,
                     'direct_referencers': sorted(set(referencers)), 'direct_dependencies': sorted(set(dependencies))})

        if not referencers:
            orphan_count += 1
            findings.append(Finding(
                category=CATEGORY_ORGANIZATION,
                severity="low",
                summary="AssetRegistry reported no package referencers; runtime/code references remain unknown",
                asset=asset,
                evidence={
                    "referencer_count": 0,
                    "scope_paths": list(scope_paths),
                    "basis": "AssetRegistry package referencers, not a deletion proof; dynamically constructed soft paths, code and unloaded registry data may be absent",
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
    report = {
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
        "assets": rows,
        "coverage": {'enumeration_complete': not truncated, 'observed': len(assets),
                     'unknown': ['runtime_frame_timing', 'resident_memory', 'soft_string_references', 'compile_lint_not_run']},
        "runtime_performance": {'status': 'not_sampled', 'gt_ms': None, 'rt_ms': None, 'gpu_ms': None},
        "rule_status": "supplied_policy" if naming_rules else "project_rule_missing",
        "measurement_source": 'UE official AssetRegistry tools; dependency counts are not runtime cost',
        "mutations": "none; this aggregation is read-only by construction",
    }
    report.update(mode='project', scope=list(scope_paths), mutation='none')
    report.update(_metric_summary(call,assets))
    return report


def _metric_summary(call, assets):
    if not hasattr(call,'asset_metrics'):
        return {'metrics_status':'unavailable','editor_package_bytes':None,'unknown_types':['metric_provider_unavailable']}
    rows=[]
    for start in range(0,len(assets),128):
        result=call.asset_metrics(assets[start:start+128])
        if not isinstance(result,dict) or not isinstance(result.get('rows'),list):
            raise AuditError('Metric provider returned incomplete results',result)
        rows.extend(result['rows'])
    counts={}
    for row in rows: counts[row.get('class','unknown')]=counts.get(row.get('class','unknown'),0)+1
    measured=[r for r in rows if isinstance(r.get('editor_package_bytes'),int)]
    source_art={s['path']:s for r in rows for s in r.get('source_art',[])}
    return {'metrics_status':'observed_bounded','asset_class_counts':counts,'asset_metrics':rows,
            'source_art_files':list(source_art.values()),'source_art_bytes':sum(s['bytes'] for s in source_art.values() if isinstance(s.get('bytes'),int)),
            'source_art_coverage':'declared AssetImportData only; unavailable sizes and untracked sidecars are unknown',
            'editor_package_bytes':sum(r['editor_package_bytes'] for r in measured),
            'disk_coverage':{'measured':len(measured),'unknown':len(rows)-len(measured)},
            'largest_asset_candidates':sorted(measured,key=lambda r:-r['editor_package_bytes'])[:20],
            'redirectors':[r['asset'] for r in rows if r.get('class')=='ObjectRedirector'],
            'missing_dependencies':[r['asset'] for r in rows if not r.get('exists') and not r['asset'].startswith('/Script/')],
            'unknown_types':sorted({r.get('class','unknown') for r in rows if r.get('detail_status')!='observed'}),
            'resident_memory_bytes':None}


def audit_content_package(call, scope_paths, *, max_assets=DEFAULT_MAX_ASSETS, max_depth=8):
    report = audit_project(call, scope_paths, max_assets=max_assets)
    seeds = [row['asset'] for row in report['assets']]
    closure, pending, truncated = set(seeds), [(p, 0) for p in seeds], report['truncated']
    direct = {row['asset']: row['direct_dependencies'] for row in report['assets']}
    while pending:
        asset, depth = pending.pop(0)
        dependencies = direct.get(asset)
        if dependencies is None:
            dependencies = _as_list(_call_readonly(call, T_GET_DEPENDENCIES, {'asset_path': asset}))
        for dependency in dependencies:
            if dependency in closure:
                continue
            if depth >= max_depth or len(closure) >= max_assets:
                truncated = True
                continue
            closure.add(dependency)
            pending.append((dependency, depth + 1))
    report.update(mode='content_package', reference_closure=sorted(closure),
                  closure_depth_limit=max_depth, closure_truncated=truncated,
                  closure_count=len(closure))
    report.update(_metric_summary(call,sorted(closure)))
    report['related_knowledge']=call.related_knowledge(sorted(closure)) if hasattr(call,'related_knowledge') else {'status':'unavailable'}
    report['truncated']=truncated
    report['coverage']['closure_complete']=not truncated
    report['coverage']['unknown'].extend(['undeclared_source_art','runtime_constructed_references'])
    return report


def audit_level(call, *, max_assets=DEFAULT_MAX_ASSETS):
    if not hasattr(call, 'level_snapshot'):
        raise AuditError('Level snapshot capability unavailable')
    observed = call.level_snapshot(max_assets)
    instances = observed.get('mesh_instances')
    if not isinstance(instances, dict):
        raise AuditError('Level snapshot missing mesh instance evidence', observed)
    metrics=_metric_summary(call, sorted(set(instances) | set(observed.get('unique_materials',[])) | set(observed.get('unique_textures',[]))))
    return {'ok': True, 'mode': 'level', **observed, **metrics,
            'unique_mesh_count': len(instances), 'total_mesh_instances': sum(instances.values()),
            'runtime_performance': {'status': 'not_sampled', 'gt_ms': None, 'rt_ms': None, 'gpu_ms': None},
            'resident_memory_bytes': None, 'mutations': 'none',
            'mutation':'none','findings':[],'confidence':'loaded_editor_world_only',
            'recommended_next_measurement':'Sample separate GT/RT/GPU traces under declared PIE or dedicated-server topology',
            'measurement_source': 'Editor actor/component enumeration; shared resources deduplicated by asset path',
            'coverage': {'unknown': ['WorldPartition_unloaded_cells', 'streaming_unloaded_levels', 'runtime_residency', 'GPU_cost']}}


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
    rules = dict(naming_rules or {})
    assets, truncated = collect_assets(call, scope_paths, max_assets=max_assets)
    findings = _naming_findings(call, assets, rules)
    return {
        "ok": True,
        "scope_paths": list(scope_paths),
        "asset_count": len(assets),
        "truncated": truncated,
        "violation_count": len(findings),
        "rule_status": "supplied_policy" if naming_rules else "project_rule_missing",
        "suggested_prefixes": {} if naming_rules else DEFAULT_NAMING_RULES,
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

    The returned plan binds revisions, complete references, dependencies and
    the Editor/view identity. apply_moves refuses a plan
    whose digest no longer matches the live evidence.
    """
    if not 1 <= len(moves) <= 8:
        raise AuditError("moves must contain between 1 and 8 explicit entries")
    normalized = [{k: str(m.get(k, '')).strip() for k in ('source', 'destination')} for m in moves]
    sources = [m['source'].casefold() for m in normalized]
    destinations = [m['destination'].casefold() for m in normalized]
    if len(set(sources)) != len(sources) or len(set(destinations)) != len(destinations) or set(sources) & set(destinations):
        raise AuditError('Duplicate, case-only, cyclic or chained move plans are unsupported')
    context = None

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
        if context is None:
            if not hasattr(call, 'snapshot'):
                raise AuditError('Move requires trusted native revision/session/view snapshot transport')
            context = call.snapshot([p for m in normalized for p in m.values()])
        source_state = context['targets'][source]
        destination_state = context['targets'][destination]
        if not source_state.get('exists') or destination_state.get('exists') or source_state.get('dirty'):
            blocked.append({'source': source, 'destination': destination, 'reason': 'source_missing_dirty_or_destination_exists'})
            continue
        if any(kind in source_state.get('class_name', '') for kind in ('World', 'Level', 'Redirector')):
            blocked.append({'source': source, 'destination': destination, 'reason': 'special_asset_unsupported'})
            continue
        referencers = _as_list(_call_readonly(call, T_GET_REFERENCERS, {"asset_path": source}))
        dependencies = _as_list(_call_readonly(call, T_GET_DEPENDENCIES, {"asset_path": source}))
        entries.append({
            "source": source,
            "destination": destination,
            "referencer_count": len(referencers),
            "redirector_expected": len(referencers) > 0,
            "referencers": sorted(set(referencers)),
            "dependencies": sorted(set(dependencies)),
            "referencers_hash": _digest(sorted(set(referencers))),
            "dependencies_hash": _digest(sorted(set(dependencies))),
            "source_state": source_state,
            "destination_state": destination_state,
        })

    plan = {
        "entries": entries,
        "allow_roots": list(allow_roots),
        "context": {k: v for k, v in (context or {}).items() if k != 'targets'},
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
    if source.split('/')[1].casefold() != destination.split('/')[1].casefold():
        return 'cross-plugin/root moves are unsupported'
    for path in (source, destination):
        if not any(path == root or path.startswith(root.rstrip("/") + "/") for root in allow_roots):
            return f"path '{path}' is outside the authorized roots {list(allow_roots)}"
        if ".." in path:
            return f"path '{path}' contains a traversal segment"
        if not re.fullmatch(r'/[A-Za-z][A-Za-z0-9_]*/[A-Za-z0-9_/]+', path) or any(p in path for p in ('__ExternalActors__', '__ExternalObjects__')):
            return 'Canonical package paths only; World Partition/OFPA unsupported'
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
    confirm=True. The digest is re-derived from the full live contract: if the
    evidence changed, the plan is refused rather than silently re-planned.
    """
    if not confirm:
        raise AuditError("apply_moves requires confirm=True; a plan is not an authorization")
    entries = list((plan or {}).get("entries") or [])
    if not entries:
        raise AuditError("plan contains no entries")

    if _digest(plan) != plan_digest:
        raise AuditError('Move plan digest mismatch')
    if not hasattr(call, 'move_readback'):
        raise AuditError('Native redirector and referencer readback transport is required')
    current = plan_moves(call, entries, allow_roots=plan['allow_roots'])
    revalidated = current['plan']
    live_digest = _digest(revalidated)
    if not current['ok'] or live_digest != plan_digest:
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
            actual = _call_readonly(call, T_MOVE, {
                "path": entry["source"],
                "new_path": entry["destination"],
            })
            if actual is False or (isinstance(actual, dict) and actual.get('returnValue') is False):
                raise AuditError('Native move returned false')
            after = call.snapshot([entry['destination']])
            target = after['targets'][entry['destination']]
            unexpected_dirty = set(after.get('dirty_packages', [])) - set(plan['context'].get('dirty_packages', [])) - {p for e in entries for p in (e['source'], e['destination'])} - {p for e in entries for p in e['referencers']}
            if not target.get('exists') or target.get('class_name') != entry['source_state']['class_name'] or unexpected_dirty:
                raise AuditError('Move readback/Dirty mismatch; needs_reconciliation', after)
            proof = call.move_readback(entry)
            if not proof.get('ok'):
                raise AuditError('Move referencer/redirector readback failed; needs_reconciliation', proof)
        except AuditError as exc:
            # Stop at the first failure: continuing would leave a half-applied
            # plan whose remaining entries were never re-validated.
            failed.append({"source": entry["source"], "error": str(exc), 'evidence': exc.evidence})
            break
        moved.append({**entry, 'readback': proof})

    return {
        "ok": not failed,
        "moved": moved,
        "moved_count": len(moved),
        "failed": failed,
        "remaining": len(revalidated["entries"]) - len(moved) - len(failed),
        "packages_saved": False,
        "side_effect_state": 'needs_reconciliation' if failed else 'complete',
        "not_attempted": entries[len(moved) + len(failed):],
        "note": "Moves are transactional in the editor but unsaved; saving belongs "
                "to the caller's ChangeSet. Redirectors remain where referencers existed.",
    }
