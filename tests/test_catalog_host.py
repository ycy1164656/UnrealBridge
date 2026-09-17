"""Request binding, stale-cursor rejection, epoch guards and client reloads."""
import json
import sys
import tempfile
import types
import unittest
from pathlib import Path
from unittest.mock import patch

from network_multiclient_v3_live_smoke import _load_server
from test_catalog_cache import AUDITED, Editor
from unreal_bridge_catalog import CatalogCache
import bridge_preflight

server = _load_server()


def binding(snapshot):
    return types.SimpleNamespace(snapshot=snapshot, project=snapshot.key[0], endpoint=snapshot.key[1],
                                 token=None, selector_project=snapshot.key[0], selector_endpoint=snapshot.key[1])


class CatalogHostTests(unittest.TestCase):
    def setUp(self):
        self.editor = Editor('A')
        self.editor.catalog[0]['tools'].append({'name': 'Test.Tools.Other', 'inputSchema': {}})
        self.cache = CatalogCache()
        self.snapshot = self.refresh()

    def refresh(self):
        return self.cache.get(self.editor.key, {'libraries': {}}, AUDITED, self.editor.fetch)

    def test_old_cursor_is_rejected_after_revision_changes(self):
        with patch.object(server, '_catalog_snapshot_for', return_value=binding(self.snapshot)):
            first = server.bridge_search_tools('', max_items=1)
        cursor = first['page']['next_cursor']
        self.assertTrue(cursor)
        self.editor.revision += 1
        second = self.refresh()
        with patch.object(server, '_catalog_snapshot_for', return_value=binding(second)):
            response = server.bridge_search_tools('', max_items=1, cursor=cursor)
        self.assertFalse(response['success'])
        self.assertEqual(response['phase'], 'pagination')
        self.assertEqual(response['catalog_metadata']['registry_revision'], '2')
        self.assertIsNone(server._CATALOG_CONTEXT.get())

    def test_decorator_pins_target_and_cleans_up_on_exception(self):
        @server._with_catalog()
        def target(project=None, endpoint=None, token=None):
            self.assertEqual(project, self.snapshot.key[0])
            self.assertEqual(endpoint, self.snapshot.key[1])
            self.assertIs(server._current_catalog(), self.snapshot)
            raise RuntimeError('test body')
        with patch.object(server, '_catalog_snapshot_for', return_value=binding(self.snapshot)):
            with self.assertRaisesRegex(RuntimeError, 'test body'):
                target(project='alias')
        self.assertIsNone(server._CATALOG_CONTEXT.get())

    def test_epoch_guard_preserves_python_and_refuses_original_on_change(self):
        current = {'editor_session_id': self.snapshot.session, 'registry_revision': self.snapshot.revision}
        unreal = types.SimpleNamespace(UnrealBridgeUE58Library=types.SimpleNamespace(
            get_official_toolset_catalog_snapshot_json=lambda _: json.dumps(current)))
        context = server._CATALOG_CONTEXT.set(binding(self.snapshot))
        try:
            source = "from __future__ import annotations\ntouched.append('run')\n# trailing comment"
            code = server._catalog_guarded_script(source)
            namespace = {'touched': []}
            with patch.dict(sys.modules, {'unreal': unreal}):
                exec(code, namespace, namespace)
                self.assertEqual(namespace['touched'], ['run'])
                current['registry_revision'] = '99'
                with self.assertRaisesRegex(RuntimeError, 'catalog_epoch_changed'):
                    exec(code, namespace, namespace)
                self.assertEqual(namespace['touched'], ['run'])
                exec(server._catalog_guarded_script(source, session_only=True), namespace, namespace)
                self.assertEqual(namespace['touched'], ['run', 'run'])
                current['editor_session_id'] = 'restarted'
                with self.assertRaisesRegex(RuntimeError, 'catalog_epoch_changed'):
                    exec(server._catalog_guarded_script(source, session_only=True), namespace, namespace)
        finally:
            server._CATALOG_CONTEXT.reset(context)

    def test_live_catalog_failure_refuses_execution_before_body(self):
        from unreal_bridge_catalog import CatalogUnavailable
        body = []
        @server._with_catalog()
        def target(project=None):
            body.append(True)
            return {'success': True}
        with patch.object(server, '_catalog_snapshot_for', side_effect=CatalogUnavailable('offline')):
            result = target()
        self.assertFalse(result['success'])
        self.assertFalse(body)

    def test_native_manifest_view_refreshes_without_reloading_server(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'manifest.json'
            path.write_text('{"libraries":{}}', encoding='utf-8')
            with patch.object(server.bridge_cli, '_MANIFEST_PATH', str(path)):
                self.assertIsNone(server._resolve_manifest_function('Perf', 'new_method')[1])
                path.write_text(json.dumps({'libraries': {'UnrealBridgePerfLibrary': {
                    'functions': {'new_method': {'params': []}}}}}), encoding='utf-8')
                self.assertEqual(server._resolve_manifest_function('Perf', 'new_method')[1], {'params': []})

    def test_handshake_and_preflight_follow_manifest_replacement(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'manifest.json'
            def write(revision):
                path.write_text(json.dumps({'manifest_hash': revision, 'registry_hash': revision,
                                            'libraries': {revision: {}}}), encoding='utf-8')
            with patch.object(server.bridge_cli, '_MANIFEST_PATH', str(path)), \
                 patch.object(server.bridge_cli, '_HANDSHAKE_CACHE', None), \
                 patch.object(server.bridge_cli, '_HANDSHAKE_STAMP', None), \
                 patch.object(bridge_preflight, '_DEFAULT_MANIFEST_PATH', str(path)), \
                 patch.object(bridge_preflight, '_MANIFEST_CACHE', None), \
                 patch.object(bridge_preflight, '_MANIFEST_STAMP', None):
                write('first')
                self.assertEqual(server.bridge_cli.client_handshake()['manifest_hash'], 'first')
                self.assertIn('first', bridge_preflight.load_manifest()['libraries'])
                write('second-longer')
                self.assertEqual(server.bridge_cli.client_handshake()['manifest_hash'], 'second-longer')
                self.assertIn('second-longer', bridge_preflight.load_manifest()['libraries'])
                path.write_text('incomplete', encoding='utf-8')
                self.assertEqual(server.bridge_cli.client_handshake()['manifest_hash'], '')
                self.assertIsNone(bridge_preflight.load_manifest())
                write('recovered')
                self.assertEqual(server.bridge_cli.client_handshake()['manifest_hash'], 'recovered')
                self.assertIn('recovered', bridge_preflight.load_manifest()['libraries'])


if __name__ == '__main__':
    unittest.main()
