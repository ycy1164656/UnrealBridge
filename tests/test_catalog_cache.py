"""Catalog freshness, isolation and fail-closed execution tests without an Editor."""
import copy
import json
import sys
import tempfile
import threading
import time
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / '.claude/skills/unreal-bridge/scripts'))
from unreal_bridge_catalog import (CatalogCache, CatalogUnavailable, JsonFileCache,
                                  compact_schema, digest, intersect_catalog, project_identity)

SCHEMA = {'type': 'object', 'properties': {'value': {'type': 'integer'}}, 'required': ['value']}
AUDITED = {'toolsets': [{'name': 'Test.Tools', 'module': 'EditorToolset', 'tools': [{
    'tool': 'Read', 'name': 'Test.Tools.Read', 'input_schema': SCHEMA,
    'schema_sha256': digest(compact_schema(SCHEMA)), 'bridge_execution': 'ReadOnly',
    'save_behavior': 'Never', 'provider': 'EpicToolsetRegistry',
}]}]}


class Editor:
    def __init__(self, name):
        self.project = f'C:/catalog-fake/{name}/Test.uproject'
        self.key = (project_identity(self.project), f'127.0.0.1:{6000 + ord(name)}')
        self.session, self.revision = name, 1
        self.catalog = [{'name': 'Test.Tools', 'tools': [
            {'name': 'Test.Tools.Read', 'inputSchema': copy.deepcopy(SCHEMA)}]}]
        self.online, self.full_reads = True, 0

    def fetch(self, include):
        if not self.online:
            raise OSError('offline')
        payload = {'success': True, 'schema': 'unrealbridge.tool_catalog.v1',
                   'editor_session_id': self.session, 'registry_revision': str(self.revision),
                   'project_path': self.project}
        if include:
            self.full_reads += 1
            payload['catalog'] = copy.deepcopy(self.catalog)
        return payload


class CatalogCacheTests(unittest.TestCase):
    def setUp(self):
        self.cache = CatalogCache()
        self.editor = Editor('A')

    def get(self, editor=None, **kwargs):
        editor = editor or self.editor
        return self.cache.get(editor.key, {'libraries': {}}, AUDITED, editor.fetch, **kwargs)

    def test_reuses_same_epoch_but_registration_and_removal_refresh(self):
        first = self.get()
        self.assertIs(first, self.get())
        self.editor.catalog[0]['tools'].append({'name': 'Test.Tools.New', 'inputSchema': {},
                                              'annotations': {'readOnlyHint': True}})
        self.editor.revision += 1
        new = self.get()
        self.assertEqual(new.catalog['tool_count'], 2)
        self.assertNotEqual(first.fingerprint, new.fingerprint)
        self.assertEqual(new.domain_registry.by_compound['test.tools|new'].access, 'Rejected')
        self.editor.catalog[0]['tools'].pop(0)
        self.editor.revision += 1
        self.assertNotIn('test.tools|read', self.get().domain_registry.by_compound)

    def test_schema_drift_cannot_inherit_trusted_access(self):
        self.editor.catalog[0]['tools'][0]['inputSchema']['properties']['value']['type'] = 'string'
        operation = self.get().domain_registry.by_compound['test.tools|read']
        self.assertEqual(operation.access, 'Rejected')
        self.assertEqual(operation.record['catalog_policy_match'], 'schema_changed')

    def test_descriptions_do_not_change_structural_permission(self):
        self.editor.catalog[0]['tools'][0]['inputSchema']['description'] = 'untrusted descriptive text'
        self.assertEqual(self.get().domain_registry.by_compound['test.tools|read'].access, 'ReadOnly')

    def test_two_editors_and_restart_are_isolated(self):
        a = self.get()
        b = Editor('B')
        b.catalog[0]['tools'] = []
        self.assertEqual(self.get(b).catalog['tool_count'], 0)
        self.assertEqual(self.get().catalog['tool_count'], 1)
        self.editor.session = 'A-restarted'
        self.editor.catalog[0]['tools'] = []
        new = self.get()
        self.assertNotEqual(a.fingerprint, new.fingerprint)
        self.assertEqual(new.catalog['tool_count'], 0)

    def test_concurrent_refresh_publishes_once_per_editor(self):
        barrier = threading.Barrier(8)
        def query(_):
            barrier.wait()
            return self.get()
        with ThreadPoolExecutor(max_workers=8) as pool:
            results = list(pool.map(query, range(8)))
        self.assertEqual(self.editor.full_reads, 1)
        self.assertTrue(all(row is results[0] for row in results))

    def test_one_editor_does_not_hold_other_editor_lock(self):
        blocked = threading.Event()
        release = threading.Event()
        def slow(include):
            blocked.set()
            self.assertTrue(release.wait(3))
            return self.editor.fetch(include)
        with ThreadPoolExecutor(max_workers=2) as pool:
            first = pool.submit(self.cache.get, self.editor.key, {}, AUDITED, slow)
            self.assertTrue(blocked.wait(1))
            try:
                self.assertEqual(pool.submit(self.get, Editor('B')).result(timeout=2).session, 'B')
            finally:
                release.set()
            first.result(timeout=3)

    def test_offline_discovery_is_stale_and_execution_is_refused(self):
        live = self.get()
        self.editor.online = False
        stale = self.get()
        self.assertTrue(stale.stale)
        self.assertFalse(live.stale)
        self.assertEqual(stale.source, 'stale_cache')
        with self.assertRaises(CatalogUnavailable):
            self.get(for_execution=True)
        self.editor.online = True
        self.assertFalse(self.get().stale)

    def test_offline_first_use_is_explicit_audited_reference(self):
        self.editor.online = False
        self.assertEqual(self.get().source, 'audited_offline')

    def test_same_revision_content_refreshes_at_ttl(self):
        now = [0.0]
        self.cache = CatalogCache(ttl=30, clock=lambda: now[0])
        first = self.get()
        self.editor.catalog[0]['tools'] = []
        now[0] = 31
        second = self.get()
        self.assertNotEqual(first.fingerprint, second.fingerprint)
        self.assertEqual(second.catalog['tool_count'], 0)

    def test_manifest_refresh_invalidates_combined_snapshot(self):
        first = self.get()
        changed = self.cache.get(self.editor.key, {'libraries': {}, 'revision': 2}, AUDITED, self.editor.fetch)
        self.assertIsNot(first, changed)
        self.assertNotEqual(first.fingerprint, changed.fingerprint)

    def test_mismatched_project_and_malformed_catalog_never_publish(self):
        first = self.get()
        self.editor.project = 'C:/unexpected/Test.uproject'
        with self.assertRaises(CatalogUnavailable):
            self.get(for_execution=True)
        self.editor.project = 'C:/catalog-fake/A/Test.uproject'
        self.editor.catalog = [{'name': 'Test.Tools', 'tools': 'invalid'}]
        self.editor.revision += 1
        self.assertEqual(self.get().fingerprint, first.fingerprint)
        with self.assertRaises(CatalogUnavailable):
            self.get(for_execution=True)

    def test_case_ambiguous_ids_are_rejected(self):
        raw = copy.deepcopy(self.editor.catalog)
        raw[0]['tools'].append({'name': 'Test.Tools.read', 'inputSchema': {}})
        with self.assertRaises(CatalogUnavailable):
            intersect_catalog(raw, AUDITED)

    def test_cache_capacity_is_bounded(self):
        self.cache = CatalogCache(max_editors=2)
        for name in ('A', 'B', 'C'):
            self.get(Editor(name))
        self.assertEqual(len(self.cache._entries), 2)

    def test_json_file_reloads_and_does_not_publish_bad_updates(self):
        cache = JsonFileCache()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'manifest.json'
            path.write_text('{"libraries": {}}', encoding='utf-8')
            first = cache.read(path)
            self.assertIs(first, cache.read(path))
            path.write_text('{"libraries": {}, "revision": 2}', encoding='utf-8')
            self.assertEqual(cache.read(path)['revision'], 2)
            path.write_text('invalid', encoding='utf-8')
            with self.assertRaises(ValueError):
                cache.read(path)
            path.unlink()
            with self.assertRaises(OSError):
                cache.read(path)


if __name__ == '__main__':
    unittest.main()
