"""The skill checkout and the selected project's facts must stay independent."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPTS = Path(__file__).resolve().parents[1] / 'skills/shooterroyal-vfx/scripts'
CATALOG = Path('docs/reference/data/308_ShooterRoyal_VFX全库选型目录.json')


class VFXProjectPathsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.project = self.base / 'project'
        catalog = self.project / CATALOG
        catalog.parent.mkdir(parents=True)
        catalog.write_text(json.dumps({'particles': [{
            'id': 'FIXTURE', 'path': '/Fixture/System.System', 'name': 'System',
            'class': 'NiagaraSystem', 'class_path': '/Script/Niagara.NiagaraSystem',
            'group': 'fixture', 'source_id': 'SRAS-FIXTURE', 'is_template': False,
        }]}), encoding='utf-8')
        control = catalog.parent / 'vfx/control'
        control.mkdir(parents=True)
        (control / 'fixture.json').write_text(json.dumps({
            'schema': 'SR-VFX-CONTROL-1', 'asset_id': 'FIXTURE', 'record_id': 'SELECTED-CONTROL',
        }), encoding='utf-8')
        (control.parent / 'recipe-fixture.json').write_text(json.dumps({
            'recipe_id': 'SELECTED-RECIPE', 'skill_id': 'SK_TEST',
        }), encoding='utf-8')

    def query(self, *args, cwd=None, project_env=None):
        env = dict(os.environ)
        env.pop('SHOOTERROYAL_PROJECT_ROOT', None)
        if project_env is not None:
            env['SHOOTERROYAL_PROJECT_ROOT'] = str(project_env)
        result = subprocess.run(
            [sys.executable, '-X', 'utf8', str(SCRIPTS / 'query_vfx.py'), *map(str, args)],
            cwd=cwd or self.base, env=env, stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0,
        )
        self.assertEqual(result.returncode, 0, result.stderr.decode('utf-8', errors='replace'))
        return json.loads(result.stdout)

    def test_explicit_project_overrides_environment_from_another_directory(self):
        result = self.query('--project-root', self.project, '--id', 'FIXTURE', project_env=self.base / 'wrong')
        self.assertEqual(result[0]['asset_id'], 'FIXTURE')
        self.assertEqual(result[0]['observation_validity']['runtime_state'], 'unknown')

    def test_project_can_be_found_from_a_child_working_directory(self):
        child = self.project / 'nested/work'
        child.mkdir(parents=True)
        self.assertEqual(self.query('--validate', cwd=child)['unique_ids'], 1)

    def test_environment_selects_project_without_checkout_relative_data(self):
        self.assertEqual(self.query('--validate', project_env=self.project)['unique_paths'], 1)

    def test_controls_and_recipes_use_the_selected_project(self):
        controls = self.query('--project-root', self.project, '--controls-for', 'FIXTURE')
        self.assertEqual(controls[0]['record_id'], 'SELECTED-CONTROL')
        recipes = self.query('--project-root', self.project, '--recipes-for', 'SK_TEST')
        self.assertEqual(recipes[0]['recipe_id'], 'SELECTED-RECIPE')


if __name__ == '__main__':
    unittest.main()
