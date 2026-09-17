"""Per-Editor live discovery, intersected with the locally audited policy.

No tool gains execution permission from live descriptions or annotations.
Snapshots publish together, and offline discovery is explicitly stale.
"""
from __future__ import annotations

import copy
import hashlib
import json
import os
import threading
import time
from dataclasses import dataclass, field, replace
from pathlib import Path
from typing import Any, Callable

from unreal_bridge_domains import OfficialDomainRegistry
from unreal_bridge_workflows import ToolIndex


class CatalogUnavailable(ValueError):
    pass


def digest(value: Any) -> str:
    return hashlib.sha256(json.dumps(value, ensure_ascii=False, sort_keys=True,
                                     separators=(',', ':')).encode('utf-8')).hexdigest()


def compact_schema(value: Any) -> Any:
    # Must agree with tools/build_ue58_access_policy.py and the native guard.
    if isinstance(value, dict):
        return {key: compact_schema(value[key]) for key in sorted(value)
                if key not in {'description', 'title', 'examples', '$comment'}}
    if isinstance(value, list):
        return [compact_schema(item) for item in value]
    return value


def project_identity(value: str | None) -> str:
    return os.path.normcase(os.path.abspath(value)).replace('\\', '/') if value else ''


def intersect_catalog(raw: Any, audited: dict) -> dict:
    if not isinstance(raw, list) or len(raw) > 1024:
        raise CatalogUnavailable('Live catalog must be an array with at most 1024 toolsets')
    trusted = {}
    modules = {}
    for group in audited.get('toolsets', []):
        modules[group['name']] = group.get('module', 'official')
        for tool in group.get('tools', []):
            key = (group['name'], tool['tool'])
            if key in trusted:
                raise CatalogUnavailable('Duplicate tool id in audited catalog')
            trusted[key] = tool
    result = {'schema_version': 1, 'provider': 'EpicToolsetRegistry', 'toolsets': []}
    seen_groups, seen_ids = set(), set()
    total = 0
    for group in raw:
        if not isinstance(group, dict) or not isinstance(group.get('name'), str):
            raise CatalogUnavailable('Malformed toolset record')
        name = group['name']
        if not name or len(name) > 512 or name.casefold() in seen_groups:
            raise CatalogUnavailable('Missing, oversized or ambiguous toolset name')
        seen_groups.add(name.casefold())
        tools = group.get('tools', [])
        if not isinstance(tools, list):
            raise CatalogUnavailable('Malformed tools array')
        rows = []
        for tool in tools:
            total += 1
            if total > 10000 or not isinstance(tool, dict) or not isinstance(tool.get('name'), str):
                raise CatalogUnavailable('Malformed or oversized live tool catalog')
            tool_name = tool['name'].removeprefix(name + '.')
            if not tool_name or len(tool_name) > 512:
                raise CatalogUnavailable('Invalid tool name')
            key = (name, tool_name)
            folded = (name.casefold(), tool_name.casefold())
            if folded in seen_ids:
                raise CatalogUnavailable('Ambiguous live tool id')
            seen_ids.add(folded)
            schema = tool.get('inputSchema', {})
            if not isinstance(schema, dict):
                raise CatalogUnavailable('Input schema must be an object')
            schema_hash = digest(compact_schema(schema))
            prior = trusted.get(key)
            approved = prior is not None and prior.get('schema_sha256') == schema_hash
            row = copy.deepcopy(prior) if approved else {
                'provider': 'EpicToolsetRegistry', 'risk': 'Unknown', 'classification': 'Rejected',
                'bridge_execution': 'Rejected', 'save_behavior': 'Never',
            }
            row.update({
                'name': name + '.' + tool_name, 'tool': tool_name,
                'description': str(tool.get('description', ''))[:8192],
                'input_schema': copy.deepcopy(schema),
                'output_schema': copy.deepcopy(tool.get('outputSchema', {})),
                'schema_sha256': schema_hash,
                'catalog_policy_match': 'exact' if approved else 'schema_changed' if prior else 'unaudited',
            })
            rows.append(row)
        result['toolsets'].append({
            'name': name, 'module': modules.get(name, 'official'),
            'version': str(group.get('version', 'Unknown'))[:128],
            'description': str(group.get('description', ''))[:8192],
            'tools': sorted(rows, key=lambda row: row['tool']), 'tool_count': len(rows),
        })
    result['toolsets'].sort(key=lambda row: row['name'])
    result['toolset_count'] = len(result['toolsets'])
    result['tool_count'] = total
    return result


@dataclass(frozen=True)
class CatalogSnapshot:
    key: tuple[str, str]
    session: str
    revision: str
    fingerprint: str
    loaded_at: float
    manifest: dict
    catalog: dict
    tool_index: ToolIndex
    domain_registry: OfficialDomainRegistry
    stale: bool = False
    source: str = 'live'
    error: str = ''

    def metadata(self) -> dict:
        return {'project': self.key[0], 'endpoint': self.key[1],
                'editor_session_id': self.session, 'registry_revision': self.revision,
                'fingerprint': self.fingerprint, 'stale': self.stale, 'source': self.source,
                'toolset_count': self.catalog.get('toolset_count', 0),
                'tool_count': self.catalog.get('tool_count', 0),
                **({'refresh_error': self.error} if self.error else {})}


@dataclass
class _Entry:
    lock: threading.Lock = field(default_factory=threading.Lock)
    snapshot: CatalogSnapshot | None = None
    files_digest: str = ''
    in_use: int = 0
    touched: float = 0


class CatalogCache:
    def __init__(self, *, max_editors: int = 16, ttl: float = 30.0,
                 clock: Callable[[], float] = time.monotonic):
        self.max_editors, self.ttl, self.clock = max_editors, ttl, clock
        self._lock = threading.Lock()
        self._entries: dict[tuple[str, str], _Entry] = {}

    def get(self, key: tuple[str, str], manifest: dict, audited: dict,
            fetch: Callable[[bool], dict], *, for_execution: bool = False,
            files_digest: str | None = None) -> CatalogSnapshot:
        files_digest = files_digest or digest([manifest, audited])
        with self._lock:
            entry = self._entries.get(key)
            if entry is None:
                if len(self._entries) >= self.max_editors:
                    candidates = [(item.touched, old) for old, item in self._entries.items() if not item.in_use]
                    if not candidates:
                        raise CatalogUnavailable('All catalog cache slots are in use')
                    del self._entries[min(candidates)[1]]
                entry = self._entries[key] = _Entry()
            entry.in_use += 1
        try:
            with entry.lock:
                try:
                    meta = fetch(False)
                    self._validate_meta(meta, key)
                    epoch = (meta['editor_session_id'], str(meta['registry_revision']))
                    old = entry.snapshot
                    if (old and (old.session, old.revision) == epoch and
                            entry.files_digest == files_digest and self.clock() - old.loaded_at < self.ttl):
                        return old
                    payload = fetch(True)
                    self._validate_meta(payload, key)
                    catalog = intersect_catalog(payload.get('catalog'), audited)
                    # One atomic publication: schema, policy, index and domain view share an epoch.
                    session, revision = payload['editor_session_id'], str(payload['registry_revision'])
                    snapshot = self._make(key, session, revision, manifest, catalog, files_digest)
                    entry.snapshot, entry.files_digest = snapshot, files_digest
                    return snapshot
                except (OSError, RuntimeError, ValueError, TypeError, KeyError) as exc:
                    if for_execution:
                        raise CatalogUnavailable(f'Live catalog unavailable; execution refused: {exc}') from exc
                    if entry.snapshot and entry.files_digest == files_digest:
                        return replace(entry.snapshot, stale=True, source='stale_cache', error=str(exc)[:512])
                    offline = self._make(key, '', 'offline', manifest, audited, files_digest)
                    return replace(offline, stale=True, source='audited_offline', error=str(exc)[:512])
        finally:
            with self._lock:
                entry.in_use -= 1
                entry.touched = self.clock()

    def _make(self, key, session, revision, manifest, catalog, files_digest):
        fingerprint = digest([key, session, revision, files_digest, catalog])
        return CatalogSnapshot(key, session, revision, fingerprint, self.clock(), manifest, catalog,
                               ToolIndex(manifest, catalog), OfficialDomainRegistry(catalog))

    @staticmethod
    def _validate_meta(payload: dict, key: tuple[str, str]) -> None:
        if not isinstance(payload, dict) or payload.get('success') is not True:
            raise CatalogUnavailable('Editor returned no successful catalog snapshot')
        if payload.get('schema') != 'unrealbridge.tool_catalog.v1':
            raise CatalogUnavailable('Unsupported catalog snapshot schema; rebuild/restart the Editor plugin')
        if not isinstance(payload.get('editor_session_id'), str) or not 1 <= len(payload['editor_session_id']) <= 128:
            raise CatalogUnavailable('Missing Editor session identity')
        revision = payload.get('registry_revision')
        if not isinstance(revision, str) or not revision.isdecimal() or len(revision) > 32:
            raise CatalogUnavailable('Invalid catalog revision')
        if key[0] and project_identity(payload.get('project_path')) != key[0]:
            raise CatalogUnavailable('Catalog belongs to a different project')


class JsonFileCache:
    """Bounded, atomic JSON reload when a generated manifest/catalog file changes."""
    def __init__(self):
        self._lock = threading.Lock()
        self._files = {}

    def read(self, path: str | Path, max_bytes: int = 32 * 1024 * 1024) -> dict:
        path = Path(path).resolve()
        with self._lock:
            stat = path.stat()
            stamp = (stat.st_mtime_ns, stat.st_size)
            old = self._files.get(path)
            if old and old[0] == stamp:
                return old[1]
            if not 0 < stat.st_size <= max_bytes:
                raise CatalogUnavailable('Manifest/catalog file size limit')
            value = json.loads(path.read_text(encoding='utf-8'))
            if not isinstance(value, dict):
                raise CatalogUnavailable('Expected manifest/catalog object')
            after = path.stat()
            if stamp != (after.st_mtime_ns, after.st_size):
                raise CatalogUnavailable('Manifest/catalog changed during read; retry')
            if len(self._files) >= 8 and path not in self._files:
                self._files.pop(next(iter(self._files)))
            self._files[path] = (stamp, value, digest(value))
            return value

    def fingerprint(self, value: dict) -> str:
        with self._lock:
            for record in self._files.values():
                if record[1] is value:
                    return record[2]
        return digest(value)
