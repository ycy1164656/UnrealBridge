import copy
import sys
import unittest
from pathlib import Path
from unittest.mock import Mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / '.claude/skills/unreal-bridge/scripts'))
from project_adapters import shooterroyal_scenarios as scenario


def request():
    return dict(schema=scenario.SCHEMA, operation='prepare', request_id='request-1',
                world_handle='ubr:world:world', player_handle='ubr:actor:player',
                world_generation='a' * 32, player_identity='local:player', scenario_id='SRSC-FRAMEWORK-PROBE')


class ShooterRoyalScenarioAdapterTests(unittest.TestCase):
    def test_defaults_are_deterministic_and_input_unchanged(self):
        before = request()
        original = copy.deepcopy(before)
        result = scenario.normalize(before)
        self.assertEqual(before, original)
        self.assertEqual('automated', result['mode'])
        self.assertEqual(1, result['seed'])

    def test_foreign_fields_are_not_script_escape_hatches(self):
        for field in ('script', 'class_path', 'console', 'save', 'asset_path'):
            with self.subTest(field=field), self.assertRaises(ValueError):
                scenario.normalize(dict(request(), **{field: 'arbitrary'}))

    def test_handle_generation_and_types_are_strict(self):
        for field, value in [('world_handle','world-name'),('player_handle','PC_0'),('world_generation','expired'),
                             ('seed',True),('lease_seconds',False),('lease_seconds',float('inf')),
                             ('mode','cleanup-everything'),('scenario_version',0)]:
            with self.subTest(field=field), self.assertRaises(ValueError):
                scenario.normalize(dict(request(), **{field:value}))
        with self.assertRaises(ValueError): scenario.normalize(dict(request(),world_generation=11111111111111111111111111111111))

    def test_client_observation_is_typed_and_cannot_enable_gate(self):
        value=dict(schema=scenario.SCHEMA,operation='prediction_begin',request_id='observer-1',
                   world_handle='ubr:world:w',player_handle='ubr:actor:p',world_generation='a'*32,run_id='b'*32)
        compile(scenario.build_script(value),'<observer>','exec')
        with self.assertRaises(ValueError): scenario.normalize(dict(value,set_enabled=True))
        with self.assertRaises(ValueError): scenario.normalize(dict(value,run_id=11111111111111111111111111111111))

    def test_mutation_requires_observed_revision(self):
        value = dict(schema=scenario.SCHEMA,operation='release',request_id='release-1',
                     world_handle='ubr:world:w',player_handle='ubr:actor:p',world_generation='a'*32,run_id='b'*32)
        with self.assertRaises(ValueError): scenario.normalize(value)
        value['expected_revision'] = 1
        self.assertEqual('release', scenario.normalize(value)['operation'])

    def test_generated_script_is_valid_python_and_scopes_native_request(self):
        value = request()
        value['player_identity'] = "local:quote'\\player"
        compile(scenario.build_script(value), '<scenario>', 'exec')
        server = Mock()
        scenario.submit(server, value, project='ShooterRoyal')
        self.assertEqual('sr-scenario:request-1', server.bridge_submit_job.call_args.kwargs['idempotency_key'])
        self.assertEqual(value['world_handle'], server.bridge_submit_job.call_args.kwargs['world_handle'])
        self.assertNotIn('no_preflight', server.bridge_submit_job.call_args.kwargs)

    def test_rejected_request_never_dispatches(self):
        server = Mock()
        with self.assertRaises(ValueError): scenario.submit(server, dict(request(), seed=True))
        server.bridge_submit_job.assert_not_called()


if __name__ == '__main__': unittest.main()
