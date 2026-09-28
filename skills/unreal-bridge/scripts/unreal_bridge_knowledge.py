#!/usr/bin/env python3
"""Local verified-fragment catalog and agent-friction records.

Both surfaces are deliberately local files plus a rebuildable index rather than
new MCP tools: the content is the truth, the index is derived, and a corrupted
or deleted index must never be able to lose an entry.

Two rules this module exists to enforce:

* A fragment's verification is NOT a permanent boolean.  An entry records the
  exact environment it was verified against (engine version, plugin version,
  manifest hash); asked about under a different environment it reports `stale`,
  because "it worked once" is not evidence that it works now.
* Friction records are capped, deduplicated by signature and scrubbed of
  absolute user paths before they are written, so the log cannot grow without
  bound or quietly accumulate machine-identifying detail.

Like the other orchestration modules, this imports neither MCP nor Unreal.
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import time
import uuid
import threading
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Sequence

SCHEMA_VERSION = 2
_KNOWLEDGE_LOCK = threading.RLock()

STATUS_VERIFIED = "verified_in_scope"
STATUS_STALE = "stale"
STATUS_UNVERIFIED = "unverified"
STATUS_FAILED = "known_limited"
STATUSES = (STATUS_VERIFIED, STATUS_STALE, STATUS_UNVERIFIED, STATUS_FAILED)

MAX_FRICTION_RECORDS = 500
MAX_EVIDENCE_CHARS = 4000


class KnowledgeError(RuntimeError):
    """Raised when a catalog or friction operation cannot be trusted."""


def _now() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def _sha256(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def _atomic_json(path: Path, value: Any) -> None:
    data = (json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2) + '\n').encode('utf-8')
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + '.' + uuid.uuid4().hex + '.tmp')
    with temporary.open('xb') as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)
    if path.read_bytes() != data:
        raise KnowledgeError('Atomic write readback mismatch: ' + path.name)


def _binding(entry: dict) -> dict:
    return {key: entry.get(key) for key in (
        'fragment_sha256', 'manifest_version', 'acceptance_hash', 'adapter_source_sha256',
        'required_plugins_hash', 'dependency_hash', 'graph_type')}


def _slug(value: str) -> str:
    slug = re.sub(r"[^a-zA-Z0-9._-]+", "-", str(value)).strip("-").lower()
    if not slug:
        raise KnowledgeError("name produced an empty slug")
    return slug[:80]


def _scrub(text: str) -> str:
    """Remove absolute user paths and drive letters from free text.

    Friction reports quote real errors, which routinely embed a home directory.
    The report is useful without it.
    """
    scrubbed = re.sub(r"[A-Za-z]:[\\/](?:Users|home)[\\/][^\\/\s\"']+", "<user-home>", text)
    scrubbed = re.sub(r"/(?:home|Users)/[^/\s\"']+", "<user-home>", scrubbed)
    return re.sub(r'(?i)(token|password|secret|authorization)(\s*[:=]\s*)([^\s,;]+)', r'\1\2<redacted>', scrubbed)


@dataclass
class Environment:
    """The exact environment a verification result is bound to."""

    engine_version: str = ""
    plugin_version: str = ""
    manifest_hash: str = ""

    def to_dict(self) -> Dict[str, str]:
        return {
            "engine_version": self.engine_version,
            "plugin_version": self.plugin_version,
            "manifest_hash": self.manifest_hash,
        }

    @staticmethod
    def from_dict(data: Optional[Dict[str, Any]]) -> "Environment":
        data = data or {}
        return Environment(
            engine_version=str(data.get("engine_version") or ""),
            plugin_version=str(data.get("plugin_version") or ""),
            manifest_hash=str(data.get("manifest_hash") or ""),
        )

    def matches(self, other: "Environment") -> bool:
        # An unrecorded field cannot confirm a match, so it fails closed.
        if not (self.engine_version and self.plugin_version and self.manifest_hash):
            return False
        return self.to_dict() == other.to_dict()


class FragmentCatalog:
    """Verified K2 fragments stored as individual files under `root`."""

    def __init__(self, root: os.PathLike[str] | str, *, verification_reader=None):
        self.root = Path(root)
        self.entries_dir = self.root / "fragments"
        self.verification_reader = verification_reader

    # ── storage ────────────────────────────────────────────────

    def add(
        self,
        name: str,
        fragment_text: str,
        *,
        source_blueprint: str,
        source_graph: str,
        environment: Environment,
        status: str = STATUS_UNVERIFIED,
        evidence: Optional[Dict[str, Any]] = None,
        external_dependencies: Sequence[str] = (),
        adapts_to: Sequence[str] = (),
        fragment_id: Optional[str] = None,
        revision: str = "1",
        manifest: Optional[Dict[str, Any]] = None,
    ) -> Dict[str, Any]:
        if status not in (STATUS_UNVERIFIED, 'imported_unverified'):
            raise KnowledgeError('add cannot assign verification; use promote with a trusted verification run')
        if not fragment_text.strip():
            raise KnowledgeError("fragment_text is empty")
        if not name.strip():
            raise KnowledgeError('name is required')
        identity = str(uuid.UUID(fragment_id)) if fragment_id else str(uuid.uuid4())
        if not re.fullmatch(r'[A-Za-z0-9_-]{1,40}', revision):
            raise KnowledgeError('Invalid revision')
        slug = identity + '-' + revision
        manifest = manifest or {}
        entry = {
            "schema_version": SCHEMA_VERSION,
            "name": name,
            "slug": slug,
            "fragment_id": identity,
            "revision": revision,
            "manifest_version": str(manifest.get('manifest_version', '1')),
            "acceptance_hash": str(manifest.get('acceptance_hash', '')),
            "adapter_source_sha256": str(manifest.get('adapter_source_sha256', '')),
            "required_plugins_hash": str(manifest.get('required_plugins_hash', '')),
            "dependency_hash": str(manifest.get('dependency_hash', '')),
            "graph_type": str(manifest.get('graph_type', 'K2')),
            "use_case": str(manifest.get('use_case', '')),
            "limitations": list(manifest.get('limitations', [])),
            "fragment_sha256": _sha256(fragment_text),
            "fragment_text": fragment_text,
            "source_blueprint": source_blueprint,
            "source_graph": source_graph,
            "external_dependencies": sorted({str(d) for d in external_dependencies}),
            "adapts_to": sorted({str(a) for a in adapts_to}),
            "status": status,
            "verified_environment": environment.to_dict(),
            "evidence": evidence or {},
            "recorded_at": _now(),
        }
        self.entries_dir.mkdir(parents=True, exist_ok=True)
        path = self.entries_dir / f"{slug}.json"
        with _KNOWLEDGE_LOCK:
            if path.exists():
                existing = json.loads(path.read_text(encoding='utf-8'))
                comparable = {k: v for k, v in entry.items() if k not in ('recorded_at', 'status', 'evidence')}
                previous = {k: existing.get(k) for k in comparable}
                if comparable != previous:
                    raise KnowledgeError('Immutable fragment ID/revision conflict; create a new revision')
                return {'ok': True, 'slug': slug, 'fragment_id': identity, 'revision': revision, 'idempotent_noop': True}
            _atomic_json(path, entry)
        return {"ok": True, "slug": slug, "fragment_id": identity, "revision": revision, "path": str(path), "status": status}

    def load_all(self) -> List[Dict[str, Any]]:
        if not self.entries_dir.is_dir():
            return []
        entries: List[Dict[str, Any]] = []
        for path in sorted(self.entries_dir.glob("*.json")):
            try:
                entry = json.loads(path.read_text(encoding="utf-8"))
                if _sha256(entry.get('fragment_text', '')) != entry.get('fragment_sha256'):
                    entry['integrity_error'] = 'payload_tampered'
                entries.append(entry)
            except (OSError, json.JSONDecodeError) as exc:
                # A damaged entry is reported, never silently skipped: a catalog
                # that quietly drops content is worse than one that says so.
                entries.append({
                    "slug": path.stem,
                    "status": STATUS_FAILED,
                    "error": f"unreadable catalog entry: {exc}",
                })
        return entries

    def rebuild_index(self) -> Dict[str, Any]:
        """Regenerate the derived index from the entry files themselves."""
        entries = self.load_all()
        index = {
            "schema_version": SCHEMA_VERSION,
            "generated_at": _now(),
            "entry_count": len(entries),
            "entries": [
                {
                    "slug": e.get("slug"),
                    "name": e.get("name"),
                    "status": e.get("status"),
                    "fragment_sha256": e.get("fragment_sha256"),
                    "source_blueprint": e.get("source_blueprint"),
                    "external_dependencies": e.get("external_dependencies", []),
                    "adapts_to": e.get("adapts_to", []),
                }
                for e in entries
            ],
        }
        self.root.mkdir(parents=True, exist_ok=True)
        _atomic_json(self.root / 'index.json', index)
        return index

    # ── retrieval ──────────────────────────────────────────────

    def query(
        self,
        current_environment: Environment,
        *,
        text: str = "",
        require_verified: bool = False,
        current_binding: Optional[Dict[str, Any]] = None,
        limit: int = 20,
    ) -> Dict[str, Any]:
        """Search the catalog, re-deciding each entry's status for *this* editor.

        An entry verified against a different engine/plugin/manifest is
        downgraded to `stale` in the result. The stored record is untouched:
        it remains the evidence of what was verified, and when.
        """
        needle = text.strip().lower()
        results: List[Dict[str, Any]] = []
        for entry in self.load_all():
            if needle:
                haystack = " ".join([
                    str(entry.get("name", "")),
                    str(entry.get("source_blueprint", "")),
                    " ".join(entry.get("adapts_to", []) or []),
                ]).lower()
                if needle not in haystack:
                    continue
            stored = entry.get("status", STATUS_UNVERIFIED)
            effective = stored
            if entry.get('integrity_error'):
                effective = entry['integrity_error']
            if stored == STATUS_VERIFIED:
                recorded = Environment.from_dict(entry.get("verified_environment"))
                if not recorded.matches(current_environment) or not current_binding or _binding(entry) != current_binding:
                    effective = STATUS_STALE
                elif not self._trusted_proof(entry):
                    effective = STATUS_STALE
                if entry.get('integrity_error'):
                    effective = entry['integrity_error']
            if require_verified and effective != STATUS_VERIFIED:
                continue
            results.append({
                "slug": entry.get("slug"),
                "name": entry.get("name"),
                "stored_status": stored,
                "effective_status": effective,
                "fragment_sha256": entry.get("fragment_sha256"),
                "source_blueprint": entry.get("source_blueprint"),
                "external_dependencies": entry.get("external_dependencies", []),
                "verified_environment": entry.get("verified_environment", {}),
                "graph_type": entry.get('graph_type'),
                "adapts_to": entry.get('adapts_to', []),
                "limitations": entry.get('limitations', []),
                "use_case": entry.get('use_case', ''),
                "verification": entry.get('verification', {}),
            })
        return {
            "ok": True,
            "query": text,
            "current_environment": current_environment.to_dict(),
            "result_count": len(results),
            "results": results[:max(1, min(100, limit))],
            "truncated": len(results) > max(1, min(100, limit)),
            "note": "effective_status is recomputed for the current environment; "
                    "a stored 'verified_in_scope' does not survive an engine, plugin "
                    "or manifest change.",
        }

    def _trusted_proof(self, entry):
        if not self.verification_reader or not entry.get('verification_reference'):
            return None
        try:
            proof = self.verification_reader(entry['verification_reference'])
            if (not isinstance(proof, dict) or proof.get('evidence_trust') != 'registered_native_job'
                    or not proof.get('native_job_id') or proof.get('binding') != _binding(entry)
                    or proof.get('environment') != entry.get('verified_environment')):
                return None
            required = ('roundtrip', 'compile', 'behavior', 'persisted', 'cold_loaded')
            if any(proof.get('verification', {}).get(key) != 'pass' for key in required):
                return None
            return proof
        except (OSError, ValueError, KeyError):
            return None

    def promote(self, fragment_id: str, revision: str, reference: dict, environment: Environment):
        identity = str(uuid.UUID(fragment_id))
        if not re.fullmatch(r'[A-Za-z0-9_-]{1,40}', revision):
            raise KnowledgeError('Invalid revision')
        path = self.entries_dir / (identity + '-' + revision + '.json')
        with _KNOWLEDGE_LOCK:
            entry = json.loads(path.read_text(encoding='utf-8'))
            if _sha256(entry['fragment_text']) != entry['fragment_sha256']:
                raise KnowledgeError('payload_tampered')
            entry.update(verification_reference=reference, verified_environment=environment.to_dict())
            if not all(_binding(entry).values()) or not environment.matches(environment):
                raise KnowledgeError('Complete environment and manifest/acceptance/adapter/plugin/dependency binding required')
            proof = self._trusted_proof(entry)
            if not proof:
                raise KnowledgeError('Trusted passing verification run does not match this fragment revision')
            entry['status'] = STATUS_VERIFIED
            entry['verification'] = {**proof['verification'], 'human': 'pending'}
            _atomic_json(path, entry)
            return {'ok': True, 'status': STATUS_VERIFIED, 'slug': entry['slug'], 'verification': entry['verification']}


class FrictionLog:
    """Structured, capped, deduplicated record of tool gaps and dead ends."""

    def __init__(self, root: os.PathLike[str] | str):
        self.root = Path(root)
        self.path = self.root / "friction.json"

    def _load(self) -> List[Dict[str, Any]]:
        if not self.path.is_file():
            return []
        try:
            data = json.loads(self.path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            raise KnowledgeError('Corrupt friction log preserved; explicit repair required: ' + str(exc)) from exc
        records = data.get("records") if isinstance(data, dict) else data
        if not isinstance(records, list) or any(not isinstance(item, dict) or not item.get('signature') for item in records):
            raise KnowledgeError('Invalid friction schema preserved; explicit repair required')
        return records

    def record(
        self,
        kind: str,
        summary: str,
        *,
        attempted: str = "",
        evidence: str = "",
        resolved_by: str = "",
        environment: Optional[Environment] = None,
        cost: Optional[Dict[str, Any]] = None,
        affected_operations: Sequence[str] = (),
    ) -> Dict[str, Any]:
        """Record one friction event.

        Deduplicated on (kind, summary, attempted): a repeated dead end
        increments an occurrence count instead of appending a near-duplicate,
        so the log stays readable and the frequency stays visible.
        """
        if not summary.strip():
            raise KnowledgeError("summary is required")
        cost = cost or {}
        if set(cost) - {'extra_tool_calls','wall_seconds','retries'} or any(v is not None and (type(v) not in (int,float) or v < 0) for v in cost.values()):
            raise KnowledgeError('Cost must contain measured nonnegative counts/time or null')
        signature = _sha256("|".join([kind, summary.strip(), attempted.strip()]))
        records = self._load()

        for record in records:
            if record.get("signature") == signature:
                record["occurrences"] = int(record.get("occurrences", 1)) + 1
                record["last_seen"] = _now()
                if resolved_by and not record.get("resolved_by"):
                    record["resolved_by"] = _scrub(resolved_by)[:MAX_EVIDENCE_CHARS]
                self._write(records)
                return {"ok": True, "signature": signature, "deduplicated": True,
                        "occurrences": record["occurrences"]}

        records.append({
            "schema_version": SCHEMA_VERSION,
            "signature": signature,
            "kind": kind,
            "summary": _scrub(summary.strip())[:1000],
            "attempted": _scrub(attempted)[:MAX_EVIDENCE_CHARS],
            "evidence": _scrub(evidence)[:MAX_EVIDENCE_CHARS],
            "resolved_by": _scrub(resolved_by)[:MAX_EVIDENCE_CHARS],
            "environment": (environment or Environment()).to_dict(),
            "measured_cost": {k:cost.get(k) for k in ('extra_tool_calls','wall_seconds','retries')},
            "affected_operations": [_scrub(str(v))[:200] for v in affected_operations[:32]],
            "bridge_backlog_candidate": kind not in ('engine_limitation','project_specific','model_mistake'),
            "first_seen": _now(),
            "last_seen": _now(),
            "occurrences": 1,
        })

        dropped = 0
        if len(records) > MAX_FRICTION_RECORDS:
            # Drop the least-recently-seen entries, never the most frequent
            # ones, so a persistent gap survives a burst of one-off noise.
            records.sort(key=lambda r: (r.get("last_seen", ""), r.get("occurrences", 1)))
            dropped = len(records) - MAX_FRICTION_RECORDS
            records = records[dropped:]

        self._write(records)
        return {"ok": True, "signature": signature, "deduplicated": False,
                "record_count": len(records), "dropped_oldest": dropped}

    def _write(self, records: List[Dict[str, Any]]) -> None:
        self.root.mkdir(parents=True, exist_ok=True)
        payload = {
            "schema_version": SCHEMA_VERSION,
            "updated_at": _now(),
            "max_records": MAX_FRICTION_RECORDS,
            "record_count": len(records),
            "records": records,
        }
        _atomic_json(self.path, payload)

    def report(self, *, min_occurrences: int = 1) -> Dict[str, Any]:
        """Rank recorded friction by frequency, for turning gaps into fixes."""
        records = [r for r in self._load() if int(r.get("occurrences", 1)) >= min_occurrences]
        records.sort(key=lambda r: (-int(r.get("occurrences", 1)), r.get("kind", "")))
        return {
            "ok": True,
            "record_count": len(records),
            "records": records,
            "top_signatures": [r['signature'] for r in records[:10]],
            "review_action": 'Review repeated measured blockers; no automatic feature expansion',
            "note": "Frequency is evidence of friction, not of a correct fix. "
                    "A proposed Skill or tool change still needs its own review.",
        }
