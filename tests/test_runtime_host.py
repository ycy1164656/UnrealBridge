"""Check real host dispatch boundaries without contacting an Editor."""
from pathlib import Path
import tempfile
import time
import unittest
from unittest.mock import patch

from network_multiclient_v3_live_smoke import _load_server


class RuntimeHostTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls): cls.server = _load_server()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.run = self.server.runtime_recipes.RuntimeRun({'schema':'unrealbridge.runtime_recipe.v1'}, None, self.temp.name)
        self.run.deadline = time.monotonic()+30
        self.run.session, self.run.pie_session = 'editor-session', 'pie-session'

    def rpc(self, op, args=None):
        return self.server._runtime_rpc.__wrapped__(op, args or {}, self.run, False, project='unused')

    def test_generated_scripts_pass_real_manifest_preflight_and_scope(self):
        for op, args in [('snapshot', {}), ('start', {'client_count':3}), ('observe', {'world_handle':'handle'}),
                         ('input', {'world_handle':'handle','input_action_path':'/ShooterRoyal/Input/IA_Test','value':[0,0,0]}),
                         ('stop', {}), ('release', {}), ('diagnostics', {})]:
            captured = []
            def submit(**kwargs):
                captured.append(kwargs)
                compile(kwargs['code'], '<runtime-script>', 'exec')
                errors, _ = self.server.bridge_cli._preflight_or_skip(kwargs['code'])
                self.assertEqual(errors, [], (op, errors))
                return {'success':True,'job_id':f'job-{op}'}
            with self.subTest(op=op), patch.object(self.server, 'bridge_submit_job', side_effect=submit), \
                 patch.object(self.server, 'bridge_get_job', return_value={'success':True,'terminal':True,'job_state':'succeeded'}), \
                 patch.object(self.server, '_last_json_output', return_value={'ok':True}):
                self.assertTrue(self.rpc(op, args)['ok'])
                self.assertEqual(captured[0]['world_handle'], args.get('world_handle'))
                self.assertTrue(self.run.report['jobs'][-1]['terminal'])

    def test_rejected_preflight_does_not_create_unknown_side_effect(self):
        with patch.object(self.server,'bridge_submit_job',return_value={'success':False,'phase':'preflight','error':'bad function'}):
            with self.assertRaises(self.server.runtime_recipes.RuntimeFault): self.rpc('start',{'client_count':1})
        self.assertFalse(self.run.unknown)
        self.assertEqual(self.run.report['jobs'][-1]['dispatch_state'],'rejected_before_dispatch')

    def test_unconfirmed_mutation_is_not_retried(self):
        with patch.object(self.server,'bridge_submit_job',return_value={'success':False,'error':'connection lost'}) as submit:
            with self.assertRaises(self.server.runtime_recipes.RuntimeFault): self.rpc('start',{'client_count':1})
            self.assertEqual(submit.call_count,1)
        self.assertTrue(self.run.unknown)
        self.assertNotIn('job_id', self.run.report['jobs'][-1])

    def test_pending_job_blocks_cleanup_mutation(self):
        self.run.report['jobs'].append({'job_id':'pending','terminal':False})
        with patch.object(self.server,'bridge_get_job',return_value={'success':True,'terminal':False}), \
             patch.object(self.server,'bridge_submit_job') as submit:
            with self.assertRaises(self.server.runtime_recipes.RuntimeFault): self.rpc('reconcile')
            submit.assert_not_called()

    def test_cancel_request_keeps_job_nonterminal(self):
        self.run.cancelled.set()
        with patch.object(self.server,'bridge_submit_job',return_value={'success':True,'job_id':'job'}), \
             patch.object(self.server,'bridge_get_job',return_value={'success':True,'terminal':False}), \
             patch.object(self.server,'bridge_cancel_job',return_value={'success':True}) as cancel:
            with self.assertRaises(self.server.runtime_recipes.RuntimeFault): self.rpc('snapshot')
            cancel.assert_called_once()
        entry = self.run.report['jobs'][-1]
        self.assertTrue(entry['cancel_requested']); self.assertFalse(entry['terminal'])


if __name__ == '__main__': unittest.main()
