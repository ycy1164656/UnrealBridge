import json
from pathlib import Path
import tempfile
import unittest
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'skills/unreal-bridge/scripts'))
import unreal_bridge_knowledge as k


class CatalogIntegrityTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.catalog = k.FragmentCatalog(self.temp.name)
        self.env = k.Environment('5.8.2', '3.3.0', 'manifest')

    def add(self, name='中文片段', text='payload', **kwargs):
        return self.catalog.add(name, text, source_blueprint='/Game/Test', source_graph='EventGraph', environment=self.env, **kwargs)

    def test_caller_cannot_mint_verified(self):
        with self.assertRaises(k.KnowledgeError):
            self.add(status=k.STATUS_VERIFIED, evidence={'passed': True})

    def test_same_display_name_is_not_identity(self):
        a, b = self.add(), self.add(text='different')
        self.assertNotEqual(a['slug'], b['slug'])
        self.assertEqual(len(self.catalog.load_all()), 2)

    def test_revision_replay_and_conflict(self):
        self.add()
        entry = self.catalog.load_all()[0]
        replay = self.add(fragment_id=entry['fragment_id'])
        self.assertTrue(replay['idempotent_noop'])
        with self.assertRaises(k.KnowledgeError):
            self.add(text='changed', fragment_id=entry['fragment_id'])

    def test_payload_tampering_detected_without_index(self):
        record = self.add()
        path = Path(record['path'])
        entry = json.loads(path.read_text(encoding='utf-8'))
        entry['fragment_text'] = 'tampered'
        path.write_text(json.dumps(entry), encoding='utf-8')
        self.assertEqual(self.catalog.query(self.env)['results'][0]['effective_status'], 'payload_tampered')

    def test_corrupt_friction_is_preserved(self):
        log = k.FrictionLog(self.temp.name)
        log.path.write_bytes(b'{broken')
        with self.assertRaises(k.KnowledgeError):
            log.record('test', 'must not replace corrupted history')
        self.assertEqual(log.path.read_bytes(), b'{broken')

    def test_missing_verification_registry_cannot_promote(self):
        self.add()
        entry = self.catalog.load_all()[0]
        with self.assertRaises(k.KnowledgeError):
            self.catalog.promote(entry['fragment_id'], '1', {'passed': True}, self.env)

    def test_secrets_are_scrubbed(self):
        self.assertNotIn('abc123', k._scrub('token=abc123 password=abc123 Authorization: abc123'))


if __name__ == '__main__':
    unittest.main()
