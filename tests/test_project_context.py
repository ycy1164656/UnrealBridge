"""Evidence correctness, filesystem boundaries and bounded UTF-8 responses."""
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / '.claude/skills/unreal-bridge/scripts'))
from unreal_bridge_project_context import ProjectContextIndex, ContextError, build_context, encoded, asset_path


class ProjectContextTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'project'
        (self.root / 'Source/Game').mkdir(parents=True)
        (self.root / 'Config').mkdir()
        self.source = self.root / 'Source/Game/Example.cpp'
        self.source.write_text('class UExample {};\nvoid UExample::Open()\n{\n'
                               'auto Ref = TEXT("/Game/UI/WBP_Menu.WBP_Menu_C");\n}\n', encoding='utf-8')
        self.index = ProjectContextIndex(self.root)

    def test_source_asset_reference_has_exact_location_hash_and_live_resolution(self):
        def assets(paths):
            self.assertEqual(paths, ['/Game/UI/WBP_Menu.WBP_Menu'])
            return {'status': 'live', 'assets': [{'path': paths[0], 'found': True,
                'class_path': '/Script/UMGEditor.WidgetBlueprint', 'dependencies': ['/Script/Game'],
                'referencers': ['/Game/Map'], 'blueprint_summary': {'parent_class_path': '/Script/Game.Example'}}]}
        result = build_context(self.index, 'WBP_Menu', asset_reader=assets)
        row = result['evidence'][0]
        self.assertEqual(row['line'], 4)
        self.assertEqual(row['symbol'], 'UExample::Open')
        self.assertEqual(row['path'], str(self.source.resolve()))
        self.assertEqual(row['asset_resolution']['/Game/UI/WBP_Menu.WBP_Menu'], 'found')
        self.assertEqual(len(row['file_sha256']), 64)
        self.assertIn('asset_referencer', [row['kind'] for row in result['evidence']])
        self.assertIn('blueprint_summary', [row['kind'] for row in result['evidence']])

    def test_edit_rename_delete_refresh_and_invalidate_cursor(self):
        self.source.write_text('Menu one\nMenu two\nMenu three\n', encoding='utf-8')
        first = build_context(self.index, 'Menu', max_items=1)
        cursor = first['next_cursor']
        self.assertTrue(cursor)
        repeated = build_context(self.index, 'Menu', max_items=1)
        self.assertEqual(first, repeated)
        self.source.write_text('Menu changed\nMenu second\n', encoding='utf-8')
        with self.assertRaisesRegex(ContextError, 'context_changed'):
            build_context(self.index, 'Menu', max_items=1, cursor=cursor)
        changed = build_context(self.index, 'Menu')
        self.assertNotEqual(first['fingerprint'], changed['fingerprint'])
        renamed = self.source.with_name('Renamed.cpp')
        self.source.rename(renamed)
        rows = build_context(self.index, 'Menu')['evidence']
        self.assertTrue(all(row['path'].endswith('Renamed.cpp') for row in rows))
        renamed.unlink()
        self.assertEqual(build_context(self.index, 'Menu')['evidence'], [])

    def test_cross_root_and_excluded_targets_fail(self):
        outside = Path(self.temp.name) / 'outside.cpp'
        outside.write_text('private', encoding='utf-8')
        for target in (str(outside), '../outside.cpp', 'Content/Fake.uasset', 'Saved/Secret.ini'):
            with self.assertRaises(ContextError, msg=target):
                build_context(self.index, 'private', [target])

    def test_symlink_escape_is_refused(self):
        outside = Path(self.temp.name) / 'outside.cpp'
        outside.write_text('secret', encoding='utf-8')
        link = self.root / 'Source/Link.cpp'
        try:
            link.symlink_to(outside)
        except OSError as exc:
            self.skipTest(f'Host does not permit test symlink creation: {exc}')
        with self.assertRaises(ContextError):
            build_context(self.index, 'secret', [str(link)])
        self.assertFalse(build_context(self.index, 'secret')['evidence'])

    def test_descriptor_and_multiline_config_credentials_are_suppressed(self):
        (self.root / 'Config/Auth.ini').write_text('[Login]\nToken=\n  private-value\nMenu=True\n', encoding='utf-8')
        (self.root / 'Secret.uproject').write_text('{"ApiKey":\n"private-value", "Menu": true}', encoding='utf-8')
        result = build_context(self.index, 'private-value Menu')
        self.assertNotIn('private-value', json.dumps(result['evidence']))
        self.assertEqual(result['coverage']['sensitive_files_suppressed'], 2)
        self.assertTrue(result['coverage_incomplete'])

    def test_budget_counts_utf8_and_pagination_has_no_duplicates(self):
        self.source.write_text('\n'.join(f'Menu 中文证据 {i} ' + '界' * 180 for i in range(30)), encoding='utf-8')
        seen, cursor = [], None
        for _ in range(40):
            result = build_context(self.index, 'Menu', max_items=3, max_bytes=4096, cursor=cursor)
            self.assertLessEqual(len(encoded(result)), 4096)
            self.assertEqual(result['output_bytes'], len(encoded(result)))
            seen += [row['line'] for row in result['evidence']]
            cursor = result['next_cursor']
            if not cursor:
                break
        self.assertEqual(seen, list(range(1, 31)))

    def test_file_and_byte_limits_report_incomplete_coverage(self):
        for name in ('B.cpp', 'C.cpp'):
            (self.root / 'Source/Game' / name).write_text('Menu', encoding='utf-8')
        result = build_context(ProjectContextIndex(self.root, max_files=1), 'Menu')
        self.assertTrue(result['coverage']['file_limit_reached'])
        self.assertTrue(result['truncated'])
        result = build_context(ProjectContextIndex(self.root, max_scan_bytes=1), 'Menu')
        self.assertTrue(result['coverage']['byte_limit_reached'])

    def test_asset_changes_invalidate_cursor_even_when_source_is_unchanged(self):
        value = ['before']
        def assets(paths):
            return {'status': 'live', 'assets': [{'path': paths[0], 'found': True, 'dependencies': list(value)}]}
        first = build_context(self.index, 'WBP_Menu', max_items=1, asset_reader=assets)
        value[0] = 'after'
        with self.assertRaisesRegex(ContextError, 'context_changed'):
            build_context(self.index, 'WBP_Menu', max_items=1, asset_reader=assets, cursor=first['next_cursor'])

    def test_source_change_during_asset_sampling_is_not_returned_as_fresh(self):
        def assets(paths):
            self.source.write_text('Menu changed while reading UE', encoding='utf-8')
            return {'assets': []}
        with self.assertRaisesRegex(ContextError, 'source_changed_during_query'):
            build_context(self.index, 'WBP_Menu', asset_reader=assets)

    def test_semantic_results_are_candidates_and_optional_failure_is_explicit(self):
        result = build_context(self.index, 'Menu', semantic_reader=lambda query: {
            'status': 'live', 'candidates': [{'path': '/Game/Similar'}]})
        self.assertEqual(result['evidence'][-1]['kind'], 'semantic_candidate')
        degraded = build_context(self.index, 'Menu', semantic_reader=lambda query: {
            'status': 'external_provider_disabled', 'candidates': []})
        self.assertEqual(degraded['coverage']['semantic_status'], 'external_provider_disabled')
        self.assertTrue(degraded['evidence'])

    def test_impact_searches_references_outside_the_target_file(self):
        other = self.root / 'Source/Game/Other.cpp'
        other.write_text('#include "Example.h"\n', encoding='utf-8')
        result = build_context(self.index, '', ['Source/Game/Example.cpp'], mode='impact')
        self.assertTrue(any(row['path'] == str(other.resolve()) for row in result['evidence']))
        with self.assertRaises(ContextError):
            build_context(self.index, '', mode='impact')

    def test_invalid_virtual_paths_and_cursors_are_rejected(self):
        for path in ('/Game/../Secret', '/Game//A', '/Game/A:Subobject'):
            with self.assertRaises(ContextError):
                asset_path(path)
        with self.assertRaises(ContextError):
            build_context(self.index, 'Menu', cursor='invalid')


if __name__ == '__main__':
    unittest.main()
