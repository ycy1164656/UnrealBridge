import json
import tempfile
import threading
import time
import unittest
from pathlib import Path
from test_external_session_host import sessions
import unreal_bridge_external_mcp as control


class ExternalControlConcurrency(unittest.TestCase):
    def test_stop_cancels_wait_then_serializes_owned_cleanup(self):
        with tempfile.TemporaryDirectory() as directory:
            project=Path(directory)/'test.uproject'; project.write_text('{}',encoding='utf8')
            class Run:
                def __init__(self):
                    self.root=Path(directory)/'run'; self.root.mkdir(); self.lock=threading.RLock(); self.cancel_event=threading.Event()
                    self.record={'status':'new','operations':[]}; self.processes={}; self.started=threading.Event(); self.events=[]
                def persist(self): sessions.write_json(self.root/'session.json',self.record)
                def request_cancel(self): self.cancel_event.set()
                def start_session(self):
                    self.events.append('start'); assert self.record.get('control_receipts'),'Receipt must precede the worker'
                    self.started.set()
                    if not self.cancel_event.wait(3): raise AssertionError('Stop did not cancel an owned wait')
                    self.events.append('cancelled'); raise RuntimeError('cancelled')
                def stop_owned_session(self):
                    self.events.append('cleanup'); self.record['status']='cleaned'; self.persist(); return {'status':'cleaned'}
                def get_session_state(self): return dict(self.record)
            run=Run(); run.persist(); run_id='a'*32; key=(str(project.resolve()),run_id)
            control._RUNS[key]={'run':run,'managers':{}}
            def req(operation,id,**fields): return dict(schema='unrealbridge.external.control.v1',operation=operation,uproject=str(project),run_id=run_id,request_id=id,**fields)
            try:
                started=control.dispatch(req('start','start-one',spec={}))
                self.assertTrue(run.started.wait(2))
                with self.assertRaises(ValueError): control.dispatch(req('checkpoint','different-mutation'))
                stopped=control.dispatch(req('stop','stop-one'))
                deadline=time.monotonic()+3
                while time.monotonic()<deadline:
                    state=control.dispatch(req('status','read',operation_id=stopped['operation_id']))
                    if not state['executor_active']: break
                    time.sleep(.02)
                self.assertEqual('succeeded',state['operation']['status'])
                self.assertEqual(['start','cancelled','cleanup'],run.events)
                replay=control.dispatch(req('stop','stop-one'))
                self.assertTrue(replay['idempotent_replay']); self.assertEqual(stopped['operation_id'],replay['operation_id'])
                original=control.dispatch(req('status','read',operation_id=started['operation_id']))
                self.assertEqual('cancelled',original['operation']['status'])
            finally:
                run.cancel_event.set(); control._RUNS.pop(key,None)

    def test_cancelled_wait_does_not_poll_or_spawn(self):
        run=object.__new__(sessions.ExternalSession); run.cancel_event=threading.Event(); run.cancel_event.set(); run.cleaning=False
        with self.assertRaisesRegex(RuntimeError,'cancelled'): run.wait('server',lambda s:True,1)
        with self.assertRaisesRegex(RuntimeError,'Cancelled'): run.spawn('server')

    def test_receipt_budget_preserves_cleanup_slot(self):
        with tempfile.TemporaryDirectory() as directory:
            project=Path(directory)/'test.uproject'; project.write_text('{}',encoding='utf8')
            class Run:
                def __init__(self):
                    self.root=Path(directory)/'run'; self.root.mkdir(); self.lock=threading.RLock()
                    self.record={'control_receipts':{str(i):{} for i in range(63)}}; self.processes={}; self.stops=0
                def persist(self): sessions.write_json(self.root/'session.json',self.record)
                def stop_owned_session(self): self.stops+=1; return {'status':'cleaned'}
            run=Run(); run.persist(); run_id='b'*32; key=(str(project.resolve()),run_id)
            control._RUNS[key]={'run':run,'managers':{}}
            request=dict(schema='unrealbridge.external.control.v1',uproject=str(project),run_id=run_id)
            try:
                with self.assertRaisesRegex(ValueError,'reserved for stop'):
                    control.dispatch(dict(request,operation='checkpoint',request_id='refused'))
                stopped=control.dispatch(dict(request,operation='stop',request_id='cleanup'))
                deadline=time.monotonic()+3
                while time.monotonic()<deadline:
                    state=control.dispatch(dict(request,operation='status',operation_id=stopped['operation_id']))
                    if not state['executor_active']: break
                    time.sleep(.02)
                self.assertEqual('succeeded',state['operation']['status'])
                self.assertEqual(64,len(run.record['control_receipts'])); self.assertEqual(1,run.stops)
                self.assertTrue(control.dispatch(dict(request,operation='stop',request_id='cleanup'))['idempotent_replay'])
            finally: control._RUNS.pop(key,None)


if __name__=='__main__': unittest.main()
