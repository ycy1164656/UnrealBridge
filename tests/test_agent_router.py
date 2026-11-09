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

    def test_vfx_router_uses_selected_skill_and_backs_up_existing_entry(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'checkout'
            canonical = root / 'skills/shooterroyal-vfx'
            (canonical / 'scripts').mkdir(parents=True)
            for name in ('SKILL.md', 'scripts/query_vfx.py', 'scripts/project_paths.py'):
                (canonical / name).write_text('# canonical source')
            template = root / 'tools/templates/shooterroyal-vfx-router.md'
            template.parent.mkdir(parents=True)
            template.write_text('VFX {{SKILL}}')
            destination = Path(directory) / 'project/.agents/skills/shooterroyal-vfx'
            destination.mkdir(parents=True)
            (destination / 'SKILL.md').write_text('original project skill')
            preview = INSTALLER.install(root, destination, skill='shooterroyal-vfx')
            self.assertTrue(preview['customized'])
            self.assertEqual((destination / 'SKILL.md').read_text(), 'original project skill')
            applied = INSTALLER.install(root, destination, skill='shooterroyal-vfx',
                                        apply=True, replace_customized=True)
            self.assertEqual((Path(applied['backup']) / 'SKILL.md').read_text(), 'original project skill')
            self.assertIn(canonical.as_posix(), (destination / 'SKILL.md').read_text())
            self.assertFalse(INSTALLER.install(root, destination, skill='shooterroyal-vfx', apply=True)['changed'])
            self.assertFalse((destination / 'scripts').exists())


if __name__ == '__main__':
    unittest.main()
