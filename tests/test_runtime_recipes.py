import copy
import json
from pathlib import Path
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / '.claude/skills/unreal-bridge/scripts'))
from unreal_bridge_runtime import RuntimeRun, RuntimeFault, normalize, recovery_view
from unreal_bridge_workflows import ScenarioManager


class FakeEditor:
    def __init__(self):
        self.now = 0
        self.calls = []
        self.state = {'ok': True, 'editor_session_id': 'editor1', 'pie_session_id': '', 'pie': False,
            'dirty_content': [], 'dirty_maps': [], 'shader_jobs': 0, 'asset_jobs': 0,
            'worlds': [{'world_handle': 'editor-world', 'world_path': '/Game/Map.Map', 'world_type': 'Editor',
                'net_mode': 'Standalone', 'pie_instance': -1, 'has_begun_play': False}],
            'lease_owner': None, 'lease_pie_session_id': None}
        self.failure = None
        self.ready = True

    def sleep(self, amount): self.now += amount

    def rpc(self, op, args, run, cleanup):
        self.calls.append(op)
        if self.failure == op:
            raise OSError('Disconnected')
        if op == 'start':
            self.state.update(pie=True, pie_session_id='pie1', lease_owner=run.run_id, lease_pie_session_id='pie1')
            for i in range(args['client_count']):
                self.state['worlds'].append({'world_handle': f'world{i}', 'world_path': f'/Game/UEDPIE_{i}_Map.Map' if self.ready else '/Temp/Untitled.Untitled',
                    'world_type': 'PIE', 'has_begun_play': True, 'pie_instance': i, 'net_mode': 'ListenServer' if i == 0 else 'Client'})
        elif op == 'stop':
            assert self.state['lease_owner'] == run.run_id and self.state['pie_session_id'] == run.pie_session
            self.state.update(pie=False, pie_session_id='')
            self.state['worlds'] = self.state['worlds'][:1]
        elif op == 'release':
            self.state.update(lease_owner=None, lease_pie_session_id=None)
        elif op == 'observe':
            return {**copy.deepcopy(self.state), 'has_begun_play': True, 'controller_available': True, 'pawn_available': True, 'actor_count': 5}
        return copy.deepcopy(self.state)


def spec(mode='owned_pie', steps=None):
    return {'schema': 'unrealbridge.runtime_recipe.v1', 'mode': mode, 'client_count': 3, 'timeout_seconds': 10,
            'ready_timeout_seconds': 2, 'steps': steps or []}


class RuntimeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.editor = FakeEditor()

    def run_recipe(self, recipe=None):
        return RuntimeRun(recipe or spec(), self.editor.rpc, self.temp.name, clock=lambda: self.editor.now, sleep=self.editor.sleep)

    def test_success_has_ready_worlds_and_final_cleanup(self):
        run = self.run_recipe(); r = run.execute(); final = run.cleanup()
        self.assertTrue(r['success']); self.assertTrue(final['success'])
        self.assertEqual(len(run.report['ready_worlds']), 3)
        self.assertFalse(final['final']['pie']); self.assertEqual(self.editor.calls.count('start'), 1)
        self.assertEqual(self.editor.calls.count('stop'), 1)
        stored = json.loads(run.path.read_text(encoding='utf-8'))
        self.assertEqual(stored['save_whitelist'], [])
        self.assertEqual(stored['recovery'], 'completed_no_retry')

    def test_readonly_does_not_stop_existing_pie(self):
        self.editor.state.update(pie=True, pie_session_id='user-pie')
        run = self.run_recipe(spec('observe')); run.execute(); result = run.cleanup()
        self.assertTrue(result['success']); self.assertTrue(result['final']['pie'])
        self.assertNotIn('start', self.editor.calls); self.assertNotIn('stop', self.editor.calls)

    def test_owned_mode_refuses_user_pie_and_dirty(self):
        for dirty in (False, True):
            self.editor.state.update(pie=not dirty, pie_session_id='' if dirty else 'user', dirty_content=['/Game/Unsaved'] if dirty else [])
            run = self.run_recipe()
            with self.assertRaises(RuntimeFault): run.execute()
            self.assertTrue(run.cleanup()['success'])
        self.assertNotIn('start', self.editor.calls); self.assertNotIn('stop', self.editor.calls)

    def test_failed_assertion_still_cleans(self):
        run = self.run_recipe(spec(steps=[{'id': 'wrong', 'type': 'assert', 'field': 'world_count', 'value': 9}]))
        with self.assertRaises(AssertionError): run.execute()
        self.assertTrue(run.cleanup()['success']); self.assertEqual(run.report['status'], 'failed')
        self.assertEqual(run.report['steps'][0]['actual'], 3)

    def test_wait_timeout_preserves_observation_and_cleans(self):
        run = self.run_recipe(spec(steps=[{'id': 'wait', 'type': 'wait', 'field': 'world_count', 'value': 9, 'timeout_seconds': .5}]))
        with self.assertRaises(TimeoutError): run.execute()
        self.assertTrue(run.cleanup()['success']); self.assertEqual(run.report['status'], 'timed_out')

    def test_temporary_client_map_is_not_ready(self):
        self.editor.ready = False
        run = self.run_recipe()
        with self.assertRaises(TimeoutError): run.execute()
        self.assertTrue(run.cleanup()['success']); self.assertNotIn('ready_worlds', run.report)

    def test_cancel_inside_step_cleans(self):
        run = self.run_recipe(spec(steps=[{'id': 'wait', 'type': 'wait', 'field': 'world_count', 'value': 9}]))
        original = run.sleep
        def cancel_sleep(amount):
            original(amount)
            if self.editor.now >= .5: run.cancelled.set()
        run.sleep = cancel_sleep
        with self.assertRaises(RuntimeFault): run.execute()
        self.assertTrue(run.cleanup()['success']); self.assertEqual(run.report['status'], 'cancelled')

    def test_disconnect_and_unknown_dispatch_do_not_claim_clean(self):
        run = self.run_recipe(); run.execute(); self.editor.failure = 'reconcile'
        self.assertFalse(run.cleanup()['success']); self.assertEqual(run.report['status'], 'needs_reconciliation')
        self.assertNotIn('stop', self.editor.calls)
        self.editor.failure = None
        run.unknown = True
        self.assertFalse(run.cleanup()['success']); self.assertNotIn('stop', self.editor.calls)

    def test_editor_restart_refuses_old_ownership(self):
        run = self.run_recipe(); run.execute(); self.editor.state['editor_session_id'] = 'editor2'
        self.assertFalse(run.cleanup()['success']); self.assertNotIn('stop', self.editor.calls)
        self.assertEqual(run.report['replacement_editor_observation']['editor_session_id'], 'editor2')

    def test_replaced_pie_is_never_stopped(self):
        run = self.run_recipe(); run.execute(); self.editor.state['pie_session_id'] = 'user-replacement'
        self.assertFalse(run.cleanup()['success']); self.assertNotIn('stop', self.editor.calls)

    def test_dirty_drift_prevents_success_no_save(self):
        run = self.run_recipe(); run.execute(); self.editor.state['dirty_content'] = ['/Game/UserChange']
        self.assertFalse(run.cleanup()['success']); self.assertEqual(run.report['assets_saved'], [])

    def test_scoped_semantic_input_and_ambiguous_selector(self):
        action = {'id': 'pulse', 'type': 'input', 'world': {'net_mode': 'ListenServer'},
                  'input_action_path': '/ShooterRoyal/Input/IA_Test', 'value': [0,0,0]}
        run = self.run_recipe(spec(steps=[action])); run.execute(); run.cleanup()
        self.assertEqual(run.report['steps'][0]['world_handle'], 'world0')
        action['world'] = {'net_mode': 'Client'}
        run = self.run_recipe(spec(steps=[action]))
        with self.assertRaises(RuntimeFault): run.execute()
        self.assertTrue(run.cleanup()['success'])

    def test_invalid_schema_input_and_retry_refused(self):
        for change in ({'mode':'unsafe'}, {'client_count':2}, {'timeout_seconds':float('nan')}, {'code':'x'},
                       {'steps':[{'id':'x','type':'input','world':{'net_mode':'Client'},'input_action_path':'/Engine/X','value':[0,0,0]}]}):
            with self.subTest(change=change), self.assertRaises(ValueError): normalize({**spec(), **change})

    def test_absent_host_requires_reconciliation_no_replay(self):
        state = {'status': 'running', 'steps': [{'status': 'succeeded'}]}
        self.assertEqual(recovery_view(state, False)['status'], 'needs_reconciliation')
        self.assertEqual(recovery_view(state, True)['recovery']['state'], 'continuable_in_current_host')
        self.assertEqual(recovery_view({'status':'succeeded'}, False)['recovery']['state'], 'completed_no_retry')
        self.assertEqual(state['status'], 'running')

    def test_report_retries_windows_atomic_replace(self):
        import unreal_bridge_runtime as runtime
        run = self.run_recipe()
        replace = runtime.os.replace
        attempts = []
        def occupied(src, dst):
            attempts.append(1)
            if len(attempts) <= 2: raise PermissionError('transient file lock')
            return replace(src, dst)
        with patch.object(runtime.os, 'replace', side_effect=occupied), patch.object(runtime.time, 'sleep'):
            run.persist()
        self.assertEqual(len(attempts), 3)
        self.assertEqual(json.loads(run.path.read_text(encoding='utf-8'))['run_id'], run.run_id)

    def test_scenario_finalizer_success_failure_and_cancel(self):
        for outcome in ('success', 'failure', 'cancel', 'cleanup_failure'):
            cleaned = []
            gate = threading.Event()
            def execute(step, timeout):
                gate.wait(2)
                if outcome == 'failure': raise RuntimeError('step failed')
                return {'success': True}
            def cleanup(state):
                cleaned.append(state['status'])
                return {'success': outcome != 'cleanup_failure'}
            manager = ScenarioManager(Path(self.temp.name)/outcome, execute, cleanup=cleanup)
            state = manager.submit({'steps':[{'id':'one','risk':'ReadOnly'}]})
            if outcome == 'cancel': manager.cancel(state['scenario_id'])
            gate.set()
            limit = time.monotonic()+3
            while manager._threads and time.monotonic()<limit: time.sleep(.01)
            state = manager.get(state['scenario_id'])
            self.assertEqual(len(cleaned), 1)
            self.assertEqual(state['status'], {'success':'succeeded','failure':'failed','cancel':'cancelled','cleanup_failure':'needs_reconciliation'}[outcome])


if __name__ == '__main__': unittest.main()
