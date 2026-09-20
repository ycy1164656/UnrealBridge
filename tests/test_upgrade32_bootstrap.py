import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class BootstrapTests(unittest.TestCase):
    def test_wrong_editor_cannot_generate_manifest(self):
        gen = load('gen_manifest')
        with tempfile.TemporaryDirectory() as directory:
            wanted = str(Path(directory) / 'right.uproject')
            other = str(Path(directory) / 'wrong.uproject')
            self.assertTrue(gen.manifest_matches_project({'project_path': wanted}, wanted))
            self.assertFalse(gen.manifest_matches_project({'project_path': other}, wanted))
            self.assertFalse(gen.manifest_matches_project({}, wanted))
            self.assertFalse(gen.manifest_matches_project({'project_path': wanted}, 'right.uproject'))

    def test_router_preview_conflict_backup_idempotency_and_legacy_preservation(self):
        installer = load('install_codex_skill')
        with tempfile.TemporaryDirectory() as directory:
            checkout = Path(directory) / 'checkout'
            canonical = checkout / '.claude/skills/unreal-bridge'
            (canonical / 'scripts').mkdir(parents=True)
            (canonical / 'SKILL.md').write_text('runtime', encoding='utf-8')
            (canonical / 'scripts/bridge.py').write_text('# runtime', encoding='utf-8')
            (canonical / 'scripts/bridge_manifest.json').write_text('{}', encoding='utf-8')
            template = checkout / '.codex/skills/unreal-bridge/SKILL.md'
            template.parent.mkdir(parents=True)
            template.write_text('{{CHECKOUT}} {{SKILL}}', encoding='utf-8')
            destination = Path(directory) / 'installed'
            destination.mkdir()
            target = destination / 'SKILL.md'
            target.write_text('custom instructions', encoding='utf-8')
            (destination / 'scripts').mkdir()
            legacy = destination / 'scripts/legacy.py'
            legacy.write_text('legacy', encoding='utf-8')
            self.assertTrue(installer.install(checkout, destination)['customized'])
            self.assertEqual(target.read_text(), 'custom instructions')
            with self.assertRaises(ValueError):
                installer.install(checkout, destination, apply=True)
            result = installer.install(checkout, destination, apply=True, replace_customized=True)
            self.assertEqual((Path(result['backup']) / 'SKILL.md').read_text(), 'custom instructions')
            self.assertEqual(legacy.read_text(), 'legacy')
            self.assertFalse(installer.install(checkout, destination, apply=True)['changed'])
            self.assertIn(canonical.as_posix(), target.read_text())


if __name__ == '__main__':
    unittest.main()
