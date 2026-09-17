"""Bounded local source evidence, combined with freshly sampled UE references.

This is a lexical index, not a compiler call graph. Asset and semantic readers
are injected so the index never starts an external service or writes assets.
"""
from __future__ import annotations

import base64
import hashlib
import json
import os
import re
import threading
from collections import Counter, OrderedDict
from dataclasses import dataclass
from pathlib import Path
from typing import Callable


TEXT_SUFFIXES = {'.h', '.hpp', '.cpp', '.c', '.inl', '.cs', '.ini', '.uproject', '.uplugin'}
SKIP_DIRS = {'content', 'binaries', 'intermediate', 'saved', '.git', '.vs', '.tmp',
             'deriveddatacache', 'thirdparty', 'third_party', 'node_modules'}
ASSET_LITERAL = re.compile(r'''["'](/[A-Za-z][\w]*/[^\s"'<>:;]+)["']''')
CLASS = re.compile(r'^\s*(?:class|struct)\s+(?:(?:\w+_API)\s+)?([AUFIS]\w+)\b')
FUNCTION = re.compile(r'^\s*(?:[\w:<>,*&~]+\s+)+([\w:~]+)\s*\(')
SENSITIVE = re.compile(r'''(?i)(?:api[_ -]?key|password|passwd|secret|private[_ -]?key|token|authorization|bearer)\b["']?\s*[=:]|-----BEGIN .*PRIVATE KEY''')


class ContextError(ValueError):
    pass


def encoded(value) -> bytes:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':')).encode('utf-8')


def fingerprint(value) -> str:
    return hashlib.sha256(encoded(value)).hexdigest()


def asset_path(value: str) -> str:
    if not isinstance(value, str) or not re.fullmatch(r'/[A-Za-z][\w]*/[^\s\\\"\'<>:;]+', value):
        raise ContextError('Invalid Unreal asset path')
    if len(value) > 2048 or any(part in ('.', '..', '') for part in value[1:].split('/')):
        raise ContextError('Invalid Unreal asset path')
    package = value.split('.', 1)[0]
    if package.startswith('/Script/'):
        return value
    return package + '.' + package.rsplit('/', 1)[-1]


def is_link(path: Path) -> bool:
    return path.is_symlink() or bool(getattr(path, 'is_junction', lambda: False)())


@dataclass(frozen=True)
class SourceFile:
    path: Path
    stamp: tuple[int, int]
    sha256: str
    lines: tuple[str, ...]
    symbols: tuple[tuple[int, str], ...]
    size: int


class ProjectContextIndex:
    def __init__(self, project_root: str | Path, *, max_files=4096, max_file_bytes=1024 * 1024,
                 max_scan_bytes=32 * 1024 * 1024, max_cache_bytes=16 * 1024 * 1024):
        self.root = Path(project_root).resolve(strict=True)
        if not self.root.is_dir():
            raise ContextError('Project root must be an existing directory')
        self.max_files, self.max_file_bytes = max_files, max_file_bytes
        self.max_scan_bytes, self.max_cache_bytes = max_scan_bytes, max_cache_bytes
        self._cache: OrderedDict[Path, SourceFile] = OrderedDict()
        self._cache_bytes = 0
        self._lock = threading.RLock()

    def source_path(self, value: str | Path) -> Path:
        value = str(value)
        if re.match(r'^/[A-Za-z]:[/\\]', value):
            value = value[1:]
        path = Path(value)
        if not path.is_absolute():
            path = self.root / path
        # Check before and after resolution; never traverse links/junctions.
        try:
            lexical = Path(os.path.abspath(path))
            relative = lexical.relative_to(self.root)
        except ValueError as exc:
            raise ContextError('Source target escapes the declared project root') from exc
        current = self.root
        for part in relative.parts:
            current /= part
            if is_link(current):
                raise ContextError('Source targets cannot traverse symlinks or junctions')
        resolved = path.resolve()
        if not resolved.is_relative_to(self.root):
            raise ContextError('Source target escapes the declared project root')
        if any(part.lower() in SKIP_DIRS for part in resolved.relative_to(self.root).parts[:-1]):
            raise ContextError('Source target belongs to an excluded directory')
        return resolved

    def split_targets(self, targets):
        sources, assets = [], []
        if targets is not None and (not isinstance(targets, list) or len(targets) > 32):
            raise ContextError('target_paths accepts at most 32 paths')
        for item in targets or []:
            if not isinstance(item, str):
                raise ContextError('Target paths must be strings')
            if item.startswith('/') and not re.match(r'^/[A-Za-z]:[/\\]', item):
                assets.append(asset_path(item))
            else:
                sources.append(self.source_path(item))
        return sorted(set(sources)), sorted(set(assets))

    def default_roots(self):
        roots = [self.root / name for name in ('Source', 'Config', 'Plugins')]
        return [path for path in roots if path.is_dir()] + sorted(self.root.glob('*.uproject'))

    def candidates(self, roots, coverage):
        result = set()
        visited = 0
        for root in roots:
            root = self.source_path(root)
            if not root.exists():
                coverage['missing_targets'] += 1
                continue
            if root.is_file():
                if root.suffix.lower() in TEXT_SUFFIXES:
                    result.add(root)
                else:
                    coverage['excluded_files'] += 1
                continue
            if root.name.lower() in SKIP_DIRS:
                raise ContextError('Cannot index an excluded directory')
            for directory, dirs, files in os.walk(root, followlinks=False):
                visited += 1
                if visited > self.max_files * 4:
                    coverage['directory_limit_reached'] = True
                    break
                folder = Path(directory)
                dirs[:] = sorted(name for name in dirs if name.lower() not in SKIP_DIRS and not is_link(folder / name))
                for name in sorted(files):
                    path = folder / name
                    if path.suffix.lower() not in TEXT_SUFFIXES:
                        continue
                    if is_link(path):
                        coverage['links_skipped'] += 1
                        continue
                    result.add(self.source_path(path))
                    if len(result) >= self.max_files:
                        coverage['file_limit_reached'] = True
                        break
                if coverage['file_limit_reached']:
                    break
            if coverage['file_limit_reached'] or coverage['directory_limit_reached']:
                break
        return sorted(result)

    def read(self, path: Path, coverage) -> SourceFile | None:
        try:
            path = self.source_path(path)
            stat = path.stat()
            stamp = (stat.st_mtime_ns, stat.st_size)
            if not 0 < stat.st_size <= self.max_file_bytes:
                coverage['oversized_or_empty_files'] += 1
                return None
            with self._lock:
                old = self._cache.get(path)
                if old and old.stamp == stamp:
                    self._cache.move_to_end(path)
                    return old
            with path.open('rb') as stream:
                raw = stream.read(self.max_file_bytes + 1)
            after = path.stat()
            if len(raw) > self.max_file_bytes or stamp != (after.st_mtime_ns, after.st_size) or self.source_path(path) != path:
                coverage['changed_during_read'] += 1
                return None
            text = raw.decode('utf-16' if raw[:2] in (b'\xff\xfe', b'\xfe\xff') else 'utf-8-sig')
            lines = tuple(text.splitlines())
            symbols = []
            for number, line in enumerate(lines, 1):
                match = CLASS.match(line) or FUNCTION.match(line)
                if match and match[1] not in {'if', 'for', 'while', 'switch', 'return'}:
                    symbols.append((number, match[1]))
            item = SourceFile(path, stamp, hashlib.sha256(raw).hexdigest(), lines, tuple(symbols), len(raw))
            with self._lock:
                prior = self._cache.pop(path, None)
                self._cache_bytes -= prior.size if prior else 0
                while self._cache and (len(self._cache) >= self.max_files or self._cache_bytes + item.size > self.max_cache_bytes):
                    _, evicted = self._cache.popitem(last=False)
                    self._cache_bytes -= evicted.size
                if item.size <= self.max_cache_bytes:
                    self._cache[path] = item
                    self._cache_bytes += item.size
            return item
        except (OSError, UnicodeError):
            coverage['unreadable_files'] += 1
            return None

    def collect(self, query, target_paths=None, *, mode='context'):
        if not isinstance(query, str) or len(query) > 1024:
            raise ContextError('query must be a string of at most 1024 characters')
        sources, assets = self.split_targets(target_paths)
        if mode not in ('context', 'impact'):
            raise ContextError('Invalid context mode')
        if mode == 'impact' and not (sources or assets):
            raise ContextError('Impact analysis requires target_paths')
        coverage = Counter({key: 0 for key in ('missing_targets', 'excluded_files', 'links_skipped', 'oversized_or_empty_files', 'sensitive_files_suppressed',
            'changed_during_read', 'unreadable_files', 'source_files_scanned', 'source_bytes_scanned', 'sensitive_lines_suppressed',
            'file_limit_reached', 'directory_limit_reached', 'byte_limit_reached', 'match_limit_reached')})
        terms = [term.casefold() for term in query.split() if term][:8]
        if mode == 'impact' or not terms:
            terms += [path.stem.casefold() for path in sources if path.suffix.lower() in TEXT_SUFFIXES]
            terms += [path.split('.', 1)[0].casefold() for path in assets]
        roots = self.default_roots() if mode == 'impact' or not sources else sources
        rows, revisions, stamps = [], [], []
        for path in self.candidates(roots, coverage):
            try:
                stat_size = path.stat().st_size
            except OSError:
                coverage['unreadable_files'] += 1
                continue
            if coverage['source_bytes_scanned'] + stat_size > self.max_scan_bytes:
                coverage['byte_limit_reached'] = True
                break
            source = self.read(path, coverage)
            if source is None:
                continue
            # Suppressed config still consumed the scan budget.
            coverage['source_bytes_scanned'] += source.size
            # Skip credential-bearing config/descriptor files as a whole so
            # multiline values cannot escape a line-only redaction rule.
            if path.suffix.lower() in ('.ini', '.uproject', '.uplugin') and any(SENSITIVE.search(line) for line in source.lines):
                coverage['sensitive_files_suppressed'] += 1
                continue
            revisions.append((str(path.relative_to(self.root)), source.sha256))
            stamps.append((str(path), source.stamp))
            coverage['source_files_scanned'] += 1
            symbol, section = '', ''
            symbol_map = dict(source.symbols)
            config = path.suffix.lower() == '.ini'
            for number, line in enumerate(source.lines, 1):
                if SENSITIVE.search(line):
                    coverage['sensitive_lines_suppressed'] += 1
                    continue
                symbol = symbol_map.get(number, symbol)
                if config and line.strip().startswith('[') and line.strip().endswith(']'):
                    section = line.strip()[1:-1]
                matched = sum(term in line.casefold() for term in terms)
                if terms and not matched:
                    continue
                literals = []
                for raw in ASSET_LITERAL.findall(line):
                    try:
                        normalized = asset_path(raw)
                        if not normalized.startswith('/Script/'):
                            literals.append(normalized)
                    except ContextError:
                        pass
                if not terms and not (literals or number in symbol_map):
                    continue
                kind = 'source_asset_literal' if literals else 'config_field' if config else 'source_text'
                rows.append({'kind': kind, 'path': str(path), 'line': number,
                             'symbol': section if config else symbol, 'file_sha256': source.sha256,
                             'snippet': line.strip()[:600], 'snippet_truncated': len(line.strip()) > 600,
                             'asset_paths': sorted(set(literals))[:8], 'matched_terms': matched,
                             'basis': 'lexical source text; execution and compiler call edges are not inferred'})
                if len(rows) >= 2000:
                    coverage['match_limit_reached'] = True
                    break
            if coverage['match_limit_reached']:
                break
        rows.sort(key=lambda row: (row['kind'] != 'source_asset_literal', -row['matched_terms'], row['path'], row['line']))
        return {'rows': rows, 'asset_targets': assets, 'coverage': dict(coverage),
                'source_fingerprint': fingerprint(revisions), 'source_targets': [str(p) for p in sources],
                'source_stamps': stamps}


def _page(rows, header, *, max_items, max_bytes, cursor, query_hash):
    if not isinstance(max_items, int) or not 1 <= max_items <= 200:
        raise ContextError('max_items must be 1..200')
    if not isinstance(max_bytes, int) or not 4096 <= max_bytes <= 256 * 1024:
        raise ContextError('max_bytes must be 4096..262144')
    start = 0
    if cursor:
        try:
            if not isinstance(cursor, str) or len(cursor) > 1024:
                raise ValueError('invalid cursor')
            decoded = json.loads(base64.urlsafe_b64decode(cursor.encode('ascii')))
            start = decoded['offset']
            if decoded['fingerprint'] != query_hash:
                raise ContextError('context_changed: restart pagination to refresh evidence')
            if not isinstance(start, int) or not 0 <= start <= len(rows):
                raise ValueError('invalid offset')
        except (ValueError, KeyError, TypeError, UnicodeError) as exc:
            raise ContextError(str(exc)) from exc
    result = {**header, 'evidence': [], 'total_evidence': len(rows), 'next_cursor': None,
              'truncated': False, 'skipped_oversized_evidence': 0, 'budget_bytes': max_bytes,
              'budget_encoding': 'utf8_compact_json', 'output_bytes': 0, 'fingerprint': query_hash}
    position = start
    # Reserve space for the continuation token and counters before accepting a row.
    if len(encoded(result)) + 512 > max_bytes:
        raise ContextError('Header exceeds output budget; narrow target_paths/query')
    while position < len(rows) and len(result['evidence']) < max_items:
        row = rows[position]
        trial = {**result, 'evidence': result['evidence'] + [row]}
        if len(encoded(trial)) + 512 > max_bytes:
            if not result['evidence']:
                result['skipped_oversized_evidence'] += 1
                position += 1
                continue
            break
        result['evidence'].append(row)
        position += 1
    if position < len(rows):
        result['next_cursor'] = base64.urlsafe_b64encode(encoded({'offset': position, 'fingerprint': query_hash})).decode('ascii')
    result['truncated'] = position < len(rows) or bool(result['skipped_oversized_evidence']) or bool(header.get('coverage_incomplete'))
    for _ in range(3):
        result['output_bytes'] = len(encoded(result))
    if len(encoded(result)) > max_bytes:
        raise ContextError('Output budget exceeded')
    return result


def build_context(index: ProjectContextIndex, query: str, target_paths=None, *, mode='context', max_items=20,
                  max_bytes=32768, cursor=None, asset_reader: Callable | None = None,
                  semantic_reader: Callable | None = None, catalog_metadata=None):
    collected = index.collect(query, target_paths, mode=mode)
    requested = collected['asset_targets'] + [path for row in collected['rows'] for path in row['asset_paths']]
    selected = list(dict.fromkeys(requested))[:4]
    assets = asset_reader(selected) if asset_reader and selected else {'assets': [], 'status': 'not_requested' if not selected else 'editor_unavailable'}
    semantic = semantic_reader(query) if semantic_reader and query.strip() else {'status': 'not_requested', 'candidates': []}
    for source_path, stamp in collected['source_stamps']:
        try:
            path = index.source_path(source_path)
            stat = path.stat()
            if (stat.st_mtime_ns, stat.st_size) != stamp:
                raise ContextError('source_changed_during_query: refresh evidence')
        except OSError as exc:
            raise ContextError('source_changed_during_query: refresh evidence') from exc
    by_path = {row['path']: row for row in assets.get('assets', [])}
    rows = []
    for row in collected['rows']:
        row = dict(row)
        row['asset_resolution'] = {path: ('found' if by_path[path].get('found') else 'missing') if path in by_path else 'not_sampled'
                                   for path in row['asset_paths']}
        rows.append(row)
    for item in assets.get('assets', []):
        common = {'path': item['path'], 'asset_fingerprint': fingerprint(item),
                  'sampled_package_dirty': bool(item.get('dirty')), 'basis': 'live AssetRegistry sample; unsaved graph references may not be indexed'}
        rows.append({**common, 'kind': 'asset_identity', 'found': bool(item.get('found')), 'class_path': item.get('class_path', '')})
        if item.get('blueprint_summary'):
            rows.append({**common, 'kind': 'blueprint_summary', 'summary': item['blueprint_summary'],
                         'basis': 'live Blueprint summary; class ancestry reflects its generated class'})
        for dependency in item.get('dependencies', []):
            rows.append({**common, 'kind': 'asset_dependency', 'from': item['path'], 'to': dependency})
        for referencer in item.get('referencers', []):
            rows.append({**common, 'kind': 'asset_referencer', 'from': referencer, 'to': item['path']})
    for candidate in semantic.get('candidates', [])[:20]:
        rows.append({'kind': 'semantic_candidate', 'candidate': candidate,
                     'basis': 'semantic retrieval only; does not establish a dependency'})
    coverage = {**collected['coverage'], 'asset_targets_requested': len(set(requested)),
                'asset_targets_sampled': len(by_path), 'asset_status': assets.get('status'),
                'semantic_status': semantic.get('status'), 'asset_references_truncated': bool(assets.get('truncated'))}
    incomplete = (any(value for key, value in collected['coverage'].items() if key not in ('source_files_scanned', 'source_bytes_scanned'))
                  or len(set(requested)) > len(by_path) or bool(assets.get('truncated')))
    header = {'success': True, 'schema': 'unrealbridge.project_context.v1', 'mode': mode,
              'project_root': str(index.root), 'query': query, 'coverage': coverage, 'coverage_incomplete': bool(incomplete),
              'source_fingerprint': collected['source_fingerprint'], 'asset_sampling': 'fresh_per_request',
              'impact_scope': 'lexical candidates and direct package references; not a complete compiler/Blueprint execution graph'}
    if catalog_metadata is not None:
        header['catalog_metadata'] = catalog_metadata
    if semantic.get('job_id'):
        header['semantic_job_id'] = semantic['job_id']
    if assets.get('dirty_added'):
        header['dirty_added_during_asset_read'] = assets['dirty_added']
    query_hash = fingerprint([str(index.root), query, target_paths, mode, collected['source_fingerprint'], assets, semantic, catalog_metadata])
    return _page(rows, header, max_items=max_items, max_bytes=max_bytes, cursor=cursor, query_hash=query_hash)
