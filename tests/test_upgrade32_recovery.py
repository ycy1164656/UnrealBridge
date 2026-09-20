import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'.claude/skills/unreal-bridge/scripts'))
from unreal_bridge_production import atomic_json
from unreal_bridge_recovery import RecoverySupervisor, ProjectRecoveryLock, classify_exit, redact


class RecoveryTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.root=Path(self.temp.name)
        self.project=self.root/'sample.uproject'; self.project.write_text('{}')
        self.exe=self.root/'UnrealEditor.exe'; self.exe.write_bytes(b'fixture')
        self.identity={'pid':123,'creation_time':134339000000000000,'parent_pid':10,'exe':str(self.exe)}
        self.current=dict(self.identity); self.launches=0
        self.policy={'work_order_id':'recovery-test','project':str(self.project),'exe':str(self.exe),
            'map':'/Game/Test','editor_identity':self.identity,'editor_session_id':'old','plugin_version':'3.2.0',
            'registry_hash':'registry','manifest_hash':'manifest','allow_relaunch':True,'max_restarts':1,'monitor_seconds':30}
        def launch():
            self.launches+=1
            self.current=dict(self.identity,pid=124,creation_time=self.identity['creation_time']+1)
            return self.current
        self.supervisor=RecoverySupervisor(self.policy,probe=lambda pid:self.current,launcher=launch,
            readiness=lambda identity:{'ready':True,'fresh_session':'new'})
        self.supervisor.initialize()

    def tearDown(self):
        self.temp.cleanup()

    def test_normal_cancel_pause_and_pid_reuse_never_restart(self):
        for kwargs in ({'exit_code':0,'process_signaled':True},{'exit_code':5,'cancelled':True},{'exit_code':5,'normal_exit':True}):
            self.assertEqual(classify_exit(self.identity,None,**kwargs),'stopped_normal')
        self.assertEqual(classify_exit(self.identity,self.identity,debugger=True),'debugger_or_busy')
        self.assertEqual(classify_exit(self.identity,dict(self.identity,creation_time=1)),'identity_conflict')
        self.assertEqual(classify_exit(self.identity,None),'needs_attention_unknown_exit')
        self.assertEqual(classify_exit(self.identity,self.identity,exit_code=86,process_signaled=True),'abnormal_exit_confirmed')
        self.assertEqual(classify_exit(self.identity,self.identity,exit_code=0,process_signaled=True),'stopped_normal')
        atomic_json(self.supervisor.control,{'operation':'pause'})
        self.current=None
        self.assertEqual(self.supervisor.tick(exit_code=5)['status'],'paused')
        self.assertEqual(self.launches,0)

    def test_abnormal_exit_one_restart_fresh_context_and_no_replay(self):
        self.current=None
        state=self.supervisor.tick(exit_code=5,process_signaled=True)
        self.assertEqual(state['status'],'waiting_ready'); self.assertEqual(self.launches,1)
        state=self.supervisor.tick(exit_code=5)
        self.assertEqual(state['status'],'recovered_requires_fresh_context')
        self.assertFalse(state['old_handles_valid']); self.assertEqual(state['fresh_session'],'new')
        self.supervisor.tick(exit_code=5)
        self.assertEqual(self.launches,1)

    def test_unknown_side_effect_and_interrupted_launch_are_not_replayed(self):
        ledger=self.root/'Saved/UnrealBridge/Artifacts/production/recovery-test.json'
        atomic_json(ledger,{'steps':{'save':{'phase':'dispatched','job_id':'real-job'}}})
        self.current=None
        state=self.supervisor.tick(exit_code=5,process_signaled=True)
        self.assertEqual(state['status'],'needs_reconciliation'); self.assertEqual(self.launches,0)
        self.assertEqual(state['unknown_effects'][0]['job_id'],'real-job')
        state['status']='launch_intent'; atomic_json(self.supervisor.path,state)
        self.assertEqual(self.supervisor.tick(exit_code=5)['reason'],'launch_dispatch_outcome_unknown')
        self.assertEqual(self.launches,0)

    def test_loaded_mismatch_or_child_exit_opens_circuit(self):
        self.current=None; self.supervisor.tick(exit_code=5,process_signaled=True)
        self.supervisor.readiness=lambda identity:{'ready':False,'identity_mismatch':'manifest'}
        self.assertEqual(self.supervisor.tick()['status'],'needs_reconciliation')
        state=json.loads(self.supervisor.path.read_text());state['status']='waiting_ready'
        atomic_json(self.supervisor.path,state); self.current=None
        self.assertEqual(self.supervisor.tick()['status'],'circuit_open')
        self.assertEqual(self.launches,1)

    def test_exit_code_without_kernel_termination_never_relaunches(self):
        for current in (self.identity, None):
            self.current=current
            for code in (0, 5, 86):
                self.assertEqual(self.supervisor.tick(exit_code=code)['status'],'termination_pending')
                self.assertEqual(self.launches,0)
        state=self.supervisor.tick(exit_code=86,process_signaled=True)
        self.assertEqual(state['status'],'waiting_ready')
        self.assertEqual(self.launches,1)

    def test_duplicate_supervisors_and_redaction(self):
        with ProjectRecoveryLock(self.root/'lock'):
            with self.assertRaises(ValueError):
                with ProjectRecoveryLock(self.root/'lock'):
                    self.fail('Duplicate owner acquired OS lock')
        result=redact('token=abc https://example.invalid/file?signature=private')
        self.assertNotIn('abc',result); self.assertNotIn('private',result)


if __name__=='__main__': unittest.main()
