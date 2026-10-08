import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('neutral_installer', ROOT / 'tools/install_agent_skill.py')
INSTALLER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(INSTALLER)


class AgentRouterTests(unittest.TestCase):
    def test_neutral_template_preferred_and_custom_destination_is_idempotent(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'checkout'
            canonical = root / 'skills/unreal-bridge'
            (canonical / 'scripts').mkdir(parents=True)
            for name, text in [('SKILL.md', 'canonical'), ('scripts/bridge.py', '# client'), ('scripts/bridge_manifest.json', '{}')]:
                (canonical / name).write_text(text)
            neutral = root / 'tools/templates/unreal-bridge-router.md'
            neutral.parent.mkdir(parents=True)
            neutral.write_text('# AI agents\n{{CHECKOUT}} {{SKILL}}')
            destination = Path(directory) / 'any-client/skills/unreal-bridge'
            self.assertTrue(INSTALLER.install(root, destination)['changed'])
            self.assertFalse(destination.exists())
            applied = INSTALLER.install(root, destination, apply=True)
            self.assertTrue(applied['applied'])
            self.assertIn(canonical.as_posix(), (destination / 'SKILL.md').read_text())
            self.assertFalse(INSTALLER.install(root, destination, apply=True)['changed'])
            self.assertFalse((destination / 'scripts').exists())


if __name__ == '__main__':
    unittest.main()
