"""Durable host operations for the independent, project-owned runtime observer."""
from pathlib import Path
import hashlib
import json
import threading
import unreal_bridge_sessions as sessions
from unreal_bridge_workflows import ScenarioManager

_LOCK=threading.RLock()
_RUNS={}


def dispatch(request):
    common={'schema','operation','uproject','run_id','request_id'}
    extra={'start':{'spec'},'state':set(),'status':{'operation_id'},'join':{'logical_id','after_sequence'},
           'disconnect':{'participant_id','mode'},'reconnect':{'participant_id'},'checkpoint':set(),'stop':set()}
    if not isinstance(request,dict) or request.get('schema')!='unrealbridge.external.control.v1' or request.get('operation') not in extra:
        raise ValueError('Typed external control operation required')
    op=request['operation']
    if set(request)-common-extra[op]: raise ValueError('Unexpected control fields')
    run_id=sessions.identifier(request.get('run_id'),32)
    project=sessions.safe_path(request.get('uproject',''))
    if project.suffix.lower()!='.uproject' or not project.is_file(): raise ValueError('Explicit project file required')
    key=(str(project),run_id)
    if op not in ('state','status'): sessions.identifier(request.get('request_id'))
    with _LOCK:
        found=_RUNS.get(key)
        if not found:
            root=project.parent/'Saved/UnrealBridge/ExternalSessions'/run_id
            if root.exists(): run=sessions.ExternalSession.reconcile(project,run_id)
            elif op=='start':
                spec=request.get('spec')
                if not isinstance(spec,dict) or Path(spec.get('uproject','')).resolve()!=project: raise ValueError('Start project mismatch')
                run=sessions.ExternalSession(spec,run_id=run_id)
            else: raise ValueError('Unknown owned run')
            found={'run':run,'managers':{}}; _RUNS[key]=found
        run=found['run']; managers=found['managers']
        busy=any(bool(m._threads) for m in managers.values())
        if op=='state':
            if busy:
                return {'success':True,'observation':'cached_during_owned_operation','session':json.loads((run.root/'session.json').read_text(encoding='utf-8'))}
            return {'success':True,'session':run.get_session_state()}
        if op=='status':
            operation_id=request.get('operation_id')
            manager=managers.get(operation_id) or ScenarioManager(run.root/'operations',lambda *a:None)
            state=manager.get(operation_id)
            active=bool(operation_id in manager._threads and manager._threads[operation_id].is_alive())
            return {'success':True,'operation':state,'executor_active':active,
                    'recovery':'requires_reconciliation' if not active and state['status'] in ('queued','running','cancelling') else None}
        signature=hashlib.sha256(json.dumps(request,sort_keys=True,separators=(',',':')).encode()).hexdigest()
        receipts=run.record.setdefault('control_receipts',{})
        if request['request_id'] in receipts:
            receipt=receipts[request['request_id']]
            if receipt['input_hash']!=signature: raise ValueError('request_id reused with different input')
            return {'success':True,'idempotent_replay':True,**receipt}
        # Reserve the final durable receipt for explicit cleanup. A full
        # mutation history must never make its owned processes un-stoppable.
        if len(receipts)>=64 or (len(receipts)>=63 and op!='stop'):
            raise ValueError('Control receipt budget reached; final slot is reserved for stop')
        if busy and op!='stop': raise ValueError('An owned operation is still running; inspect it or explicitly stop this run')
        if busy:
            if found.get('stop_receipt'):
                return {'success':True,'cleanup_already_scheduled':True,**found['stop_receipt']}
            # Stop can interrupt a bounded wait without waiting for its lock.
            # The new cleanup worker acquires the same run lock after the
            # cancelled executor unwinds; process mutations never overlap.
            run.request_cancel()
            for previous in list(managers.values()):
                with previous._lock: active_ids=list(previous._threads)
                for operation_id in active_ids: previous.cancel(operation_id)
        if op=='start' and (run.processes or run.record.get('operations')):
            raise ValueError('Start intent exists; reconcile rather than respawning')
        functions={
            'start':lambda:run.start_session(),
            'checkpoint':lambda:run.checkpoint(),
            'join':lambda:run.join_client(request.get('logical_id'),after_sequence=request.get('after_sequence')),
            'disconnect':lambda:run.disconnect_client(request.get('participant_id'),request.get('mode')),
            'reconnect':lambda:run.reconnect_client(request.get('participant_id')),
            'stop':lambda:run.stop_owned_session(),
        }
        def execute(step,timeout):
            with run.lock: return functions[op]()
        manager=ScenarioManager(run.root/'operations',execute)
        # The persisted receipt is an operation record, never a promise to replay
        # a half-dispatched process or reconstruct a background executor.
        with run.lock:
            state=manager.submit({'name':f'External session {op}','run_id':run_id,'steps':[
                {'id':op,'type':'owned_external_process','retry':0,'timeout':360,'risk':'RuntimeInteraction'}]})
            managers[state['scenario_id']]=manager
            receipt={'input_hash':signature,'operation_id':state['scenario_id'],'run_id':run_id,'report_path':str(run.root/'session.json')}
            receipts[request['request_id']]=receipt; run.persist()
            if op=='stop': found['stop_receipt']=receipt
        return {'success':True,**receipt}
