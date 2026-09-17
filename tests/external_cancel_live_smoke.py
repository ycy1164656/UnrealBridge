"""Cancel a real join in progress and normally release only its two processes."""
import argparse
import json
from pathlib import Path
import sys
import time
import uuid
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'.claude/skills/unreal-bridge/scripts'))
import unreal_bridge_external_mcp as control
from unreal_bridge_sessions import SCHEMA, process_identity


def main():
    p=argparse.ArgumentParser(); p.add_argument('--project',required=True); p.add_argument('--out',required=True)
    a=p.parse_args(); out=Path(a.out); out.parent.mkdir(parents=True,exist_ok=True)
    run_id=uuid.uuid4().hex
    report={'status':'running','run_id':run_id,'assertions':[],'evidence':{}}
    def persist(): out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    def check(name,ok,evidence=None):
        if not ok: raise AssertionError((name,evidence))
        report['assertions'].append(name); persist()
    def request(op,id=None,**fields):
        return dict(schema='unrealbridge.external.control.v1',operation=op,uproject=a.project,run_id=run_id,request_id=id or uuid.uuid4().hex,**fields)
    def wait(receipt,timeout=180):
        deadline=time.monotonic()+timeout
        while True:
            result=control.dispatch(request('status',operation_id=receipt['operation_id']))
            if not result['executor_active']: return result
            if time.monotonic()>=deadline: raise TimeoutError(result)
            time.sleep(.2)
    stop_request=request('stop','final-owned-stop'); started=False
    try:
        start=control.dispatch(request('start','server-start',spec=dict(schema=SCHEMA,backend='editor_game',
            exe='C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe',uproject=a.project,
            map='/ShooterRoyal/Maps/SR_GamePlay_Field',port=28783,max_clients=1,lease_seconds=300,
            network_profile={'out_lag_ms':0,'out_loss_percent':0})))
        started=True; result=wait(start); report['evidence']['server_start']=result
        check('Independent dedicated server genuinely started',result['operation']['status']=='succeeded',result)
        joining=control.dispatch(request('join','join-in-progress',logical_id='cancel-player'))
        deadline=time.monotonic()+20
        while True:
            state=control.dispatch(request('state'))
            # During the worker, state intentionally returns the persisted
            # ownership record without competing for its process-operation lock.
            processes=state['session'].get('processes',{})
            if len(processes)==2: break
            if time.monotonic()>=deadline: raise TimeoutError(('Client not launched',state))
            time.sleep(.05)
        report['evidence']['owned_processes']=processes
        check('Both real process identities exist before cancellation',all(process_identity(x['pid']) for x in processes.values()),processes)
        original=control.dispatch(request('status',operation_id=joining['operation_id']))
        check('Join executor is still active at stop request',original['executor_active'],original)
        stopped=control.dispatch(stop_request); result=wait(stopped); report['evidence']['stop']=result
        check('Durable stop worker finishes successfully',result['operation']['status']=='succeeded',result)
        original=wait(joining); report['evidence']['cancelled_join']=original
        check('Cancelled join cannot resume after cleanup',original['operation']['status']=='cancelled',original)
        state=control.dispatch(request('state'))['session']; report['evidence']['final']=state
        check('All and only owned processes exited normally',state['status']=='cleaned' and len(state['participants'])==2 and all(not x['alive'] for x in state['participants'].values()),state)
        replay=control.dispatch(stop_request)
        check('Stop retry reuses the same durable operation',replay.get('idempotent_replay') and replay['operation_id']==stopped['operation_id'],replay)
        report['status']='passed'
    except Exception as exc:
        report.update(status='failed',error=str(exc)); raise
    finally:
        if started:
            cleanup=control.dispatch(stop_request); report['cleanup_operation']=wait(cleanup)
            report['cleanup_state']=control.dispatch(request('state'))['session']
        persist()
    print(json.dumps(dict(status=report['status'],checks=len(report['assertions']),out=str(out))))


if __name__=='__main__': main()
