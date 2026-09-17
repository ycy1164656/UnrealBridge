import copy
import sys
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'.claude/skills/unreal-bridge/scripts'))
import unreal_bridge_network_sessions as sessions
import unreal_bridge_runtime as runtime


def recipe():
    return dict(schema=sessions.SCHEMA,backend='owned_pie',topology='dedicated',remote_client_count=2,
                join_plan=[{'client_ordinal':2,'after':'ready_conditions'}],cleanup_policy='always')


class NetworkSessionsV2Tests(unittest.TestCase):
    def test_retained_session_control_dispatch_and_types(self):
        from network_multiclient_v3_live_smoke import _load_server
        server=_load_server()
        with patch.object(server,'bridge_submit_job',return_value={'job_id':'job'}) as submit:
            result=server.bridge_control_network_session.__wrapped__('stop','run','a'*32,'request')
            self.assertEqual('job',result['job_id']); self.assertEqual(1,submit.call_count)
            compile(submit.call_args.args[0],'<session-control>','exec')
            invalid=server.bridge_control_network_session.__wrapped__('join','run','a'*32,'request',True)
            self.assertFalse(invalid['success']); self.assertEqual(1,submit.call_count)

    def test_v1_keeps_its_original_count_and_bounds(self):
        for count in (1,3): self.assertEqual(count,runtime.normalize({'schema':runtime.SCHEMA,'client_count':count})['client_count'])
        with self.assertRaises(ValueError): runtime.normalize({'schema':runtime.SCHEMA,'client_count':2})
        with self.assertRaises(ValueError): runtime.normalize({'schema':runtime.SCHEMA,'timeout_seconds':301})

    def test_topology_and_late_join_are_explicit(self):
        before=recipe(); original=copy.deepcopy(before)
        self.assertEqual('dedicated',sessions.normalize(before)['topology'])
        self.assertEqual(original,before)
        for values in [dict(client_count=3),dict(topology='auto'),dict(remote_client_count=True),
                       dict(join_plan=[{'client_ordinal':3,'after':'ready_conditions'}]),
                       dict(join_plan=[{'client_ordinal':2,'after':'sleep-3'}]),
                       dict(join_plan=[{'client_ordinal':1,'after':'ready_conditions'},{'client_ordinal':2,'after':'ready_conditions'}]),
                       dict(cleanup_policy='stop-any-pie')]:
            with self.subTest(values=values),self.assertRaises(ValueError): sessions.normalize(dict(recipe(),**values))

    def test_profile_and_phase_bounds(self):
        for values in [dict(network_profile={'out_lag_ms':float('nan'),'out_loss_percent':0}),
                       dict(network_profile={'out_lag_ms':100,'out_loss_percent':100}),dict(timeouts={'start':181}),
                       dict(timeouts={'cleanup':True}),dict(ready_conditions=[{'id':'input','type':'input'}])]:
            with self.subTest(values=values),self.assertRaises(ValueError): sessions.normalize(dict(recipe(),**values))

    def test_cleanup_never_stops_existing_pie(self):
        calls=[]
        snapshot=dict(ok=True,editor_session_id='editor',pie=True,pie_session_id='user',dirty_content=[],dirty_maps=[],worlds=[],shader_jobs=0,asset_jobs=0)
        def rpc(op,args,run,cleanup): calls.append(op); return copy.deepcopy(snapshot)
        with tempfile.TemporaryDirectory() as root:
            run=sessions.NetworkSessionRun(recipe(),rpc,root)
            with self.assertRaises(runtime.RuntimeFault): run.execute()
            self.assertTrue(run.cleanup()['success'])
            self.assertNotIn('v2_start',calls); self.assertNotIn('v2_stop',calls)

    def test_uncertain_dispatch_is_never_stopped_or_replayed(self):
        calls=[]
        def rpc(op,args,run,cleanup): calls.append(op); return dict(ok=True,editor_session_id='editor')
        with tempfile.TemporaryDirectory() as root:
            run=sessions.NetworkSessionRun(recipe(),rpc,root)
            run.deadline=run.clock()+20; run.unknown=True; run.start_requested=True
            self.assertFalse(run.cleanup()['success']); self.assertEqual(['reconcile'],calls)

    def test_retained_cleanup_does_not_claim_pie_was_stopped(self):
        calls=[]
        snapshot=dict(ok=True,editor_session_id='editor',pie=True,pie_session_id='owned',dirty_content=[],dirty_maps=[],worlds=[],
                      network_session=dict(ok=True,owned_pie_session_id='owned',status='ready'))
        def rpc(op,args,run,cleanup): calls.append(op); return copy.deepcopy(snapshot)
        with tempfile.TemporaryDirectory() as root:
            run=sessions.NetworkSessionRun(dict(recipe(),cleanup_policy='retain_for_feedback'),rpc,root)
            run.deadline=run.clock()+20; run.start_requested=True; run.report['status']='succeeded'; run.baseline=snapshot
            result=run.cleanup()
            self.assertTrue(result['success']); self.assertTrue(result['retained_for_feedback'])
            self.assertEqual('awaiting_feedback',result['cleanup_state']); self.assertNotIn('v2_stop',calls)


if __name__=='__main__': unittest.main()
