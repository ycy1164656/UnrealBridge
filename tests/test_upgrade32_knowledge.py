import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / '.claude/skills/unreal-bridge/scripts'))
from unreal_bridge_project_context import ProjectContextIndex, ContextError, build_context


class KnowledgeTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        (self.root / 'Source').mkdir()
        (self.root / 'Source/Example.cpp').write_text('auto A = TEXT("/Game/Actual");\n// SimilarThing has no dependency\n', encoding='utf-8')
        self.index = ProjectContextIndex(self.root)
        self.view = {'view': 'sandbox', 'sandbox_id': 'one', 'sandbox_generation': 1, 'editor_session_id': 'editor-a'}

    def assets(self, paths):
        return {'status': 'live', 'view_identity': dict(self.view), 'assets': [
            {'path': '/Game/Actual.Actual', 'found': True, 'dependencies': ['/Game/RealDependency'],
             'blueprint_summary': {'parent_class_path': '/Script/Game.Actor'},
             'field_references': [{'kind': 'row_ref', 'field': 'Archer.Pawn', 'to': '/Game/Archer.Archer_C'}]}]}

    def test_only_evidence_builds_relations(self):
        result = build_context(self.index, '', asset_reader=self.assets, include_relations=True, max_items=100)
        relations = [row for row in result['evidence'] if row['kind'] == 'relation']
        self.assertEqual({row['relation'] for row in relations}, {'source_mentions', 'package_depends_on', 'inherits', 'row_ref'})
        self.assertFalse(any('SimilarThing' in row['to'] for row in relations))
        self.assertTrue(all(row['source']['path'] and row['source_fingerprint'] for row in relations))

    def test_sandbox_editor_and_persist_changes_invalidate_cursor(self):
        first = build_context(self.index, '', asset_reader=self.assets, include_relations=True, max_items=1)
        for key, value in [('sandbox_generation', 2), ('sandbox_id', 'two'), ('editor_session_id', 'editor-b')]:
            self.view[key] = value
            with self.assertRaisesRegex(ContextError, 'context_changed'):
                build_context(self.index, '', asset_reader=self.assets, include_relations=True, cursor=first['next_cursor'])

    def test_project_alias_is_candidate_only_and_mapping_change_invalidates(self):
        roles = self.root / 'Tools/UnrealBridge/project_roles.json'
        roles.parent.mkdir(parents=True)
        data = {'schema': 'unrealbridge.project_roles.v1', 'revision': '1', 'roles': [
            {'id': 'minion', 'aliases': ['远程兵'], 'terms': ['Actual'], 'source_targets': ['Source/Example.cpp'],
             'asset_targets': ['/Game/Actual'], 'bindings': {'pawn': 'MinionClass'}}]}
        roles.write_text(json.dumps(data), encoding='utf-8')
        first = build_context(self.index, '远程兵', asset_reader=self.assets, resolve_roles=True, include_relations=True, max_items=1)
        self.assertEqual(first['role_mapping']['roles'], ['minion'])
        data['revision'] = '2'
        roles.write_text(json.dumps(data), encoding='utf-8')
        with self.assertRaisesRegex(ContextError, 'context_changed'):
            build_context(self.index, '远程兵', asset_reader=self.assets, resolve_roles=True, include_relations=True, cursor=first['next_cursor'])


if __name__ == '__main__':
    unittest.main()
