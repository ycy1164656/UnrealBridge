"""Versioned PIE topology recipes. Native ownership remains authoritative."""
from __future__ import annotations
import copy
import math
import unreal_bridge_runtime as runtime

SCHEMA = 'unrealbridge.runtime_recipe.v2'


def normalize(recipe):
    if not isinstance(recipe, dict) or recipe.get('schema') != SCHEMA:
        raise ValueError('Expected explicit runtime_recipe.v2')
    allowed = {'schema','name','backend','topology','remote_client_count','join_plan','ready_conditions',
               'cleanup_policy','network_profile','timeouts','steps'}
    if set(recipe) - allowed:
        raise ValueError('Unknown v2 fields; v1 client_count is not reinterpreted')
    result = copy.deepcopy(recipe)
    if result.get('backend') != 'owned_pie' or result.get('topology') not in ('listen','dedicated'):
        raise ValueError('Explicit owned_pie backend and listen/dedicated topology required')
    count = result.get('remote_client_count')
    if type(count) is not int or not 1 <= count <= 4:
        raise ValueError('remote_client_count must be 1..4')
    if result.get('cleanup_policy') not in ('always','retain_for_feedback'):
        raise ValueError('Explicit cleanup_policy required')
    joins = result.setdefault('join_plan', [])
    if not isinstance(joins,list) or len(joins) >= count:
        raise ValueError('At least one remote must start; late joins are bounded by declared total')
    initial = count - len(joins)
    for ordinal, item in enumerate(joins,initial+1):
        if not isinstance(item,dict) or set(item) != {'client_ordinal','after'} or type(item['client_ordinal']) is not int \
                or item['client_ordinal'] != ordinal or item['after'] != 'ready_conditions':
            raise ValueError('Late joins require consecutive ordinals after verified ready_conditions')
    profile = result.setdefault('network_profile', {'out_lag_ms':0,'out_loss_percent':0})
    if not isinstance(profile,dict) or set(profile) != {'out_lag_ms','out_loss_percent'}:
        raise ValueError('Network profile specifies outgoing emulation for every peer; never an observed RTT')
    for field, high in [('out_lag_ms',500),('out_loss_percent',10)]:
        if type(profile[field]) is not int or not 0 <= profile[field] <= high:
            raise ValueError('Invalid bounded network emulation profile')
    timeouts = result.setdefault('timeouts', {})
    if not isinstance(timeouts,dict) or set(timeouts)-{'start','join','assert','cleanup'}:
        raise ValueError('Expected per-phase timeout budgets')
    for field,default,maximum in [('start',120,180),('join',60,120),('assert',60,120),('cleanup',30,60)]:
        value = timeouts.setdefault(field,default)
        if type(value) not in (int,float) or not math.isfinite(value) or not 1 <= value <= maximum:
            raise ValueError('Invalid phase timeout')
    conditions = result.setdefault('ready_conditions', [])
    steps = result.setdefault('steps', [])
    if not isinstance(conditions,list) or not isinstance(steps,list) or len(conditions)+len(steps)>32:
        raise ValueError('At most 32 readiness/assertion/input steps')
    if any(not isinstance(s,dict) or s.get('type') != 'wait' for s in conditions):
        raise ValueError('ready_conditions are typed wait steps')
    # Reuse the existing strict condition/input grammar, without changing v1 counts.
    normalized=runtime.normalize({'schema':runtime.SCHEMA,'mode':'owned_pie','steps':conditions+steps})['steps']
    result['ready_conditions'],result['steps']=normalized[:len(conditions)],normalized[len(conditions):]
    result['mode']='owned_pie'  # Internal Scenario risk metadata only, not accepted input.
    result['timeout_seconds']=timeouts['start']+len(joins)*timeouts['join']+timeouts['assert']+timeouts['cleanup']
    return result


class NetworkSessionRun(runtime.RuntimeRun):
    def __init__(self,recipe,rpc,root,**options):
        spec=normalize(recipe)
        super().__init__({'schema':runtime.SCHEMA,'mode':'observe'},rpc,root,**options)
        self.spec=spec
        self.is_v2=True
        self.report.update(schema='unrealbridge.runtime_report.v2',spec=spec,input_hash=runtime.digest(spec))
        self.persist()

    def call(self,op,args=None,*,cleanup=False):
        if not cleanup: self.check()
        end=min(self.clock()+20,getattr(self,'_cleanup_deadline',float('inf'))) if cleanup else min(self.clock()+20,self.deadline)
        while True:
            result=self.rpc(op,args or {},self,cleanup)
            # Catalog lookup is read-only and rejects before dispatch. Do not
            # retry any runtime mutation, uncertain dispatch, or failed native job.
            if op in ('snapshot','v2_state','reconcile','diagnostics','observe') and isinstance(result,dict) \
                    and result.get('phase')=='catalog' and result.get('retryable') and self.clock()<end:
                self.report.setdefault('catalog_read_retries',[]).append({'operation':op,'error':result.get('error','')[:300]})
                self.persist(); self.sleep(.25); continue
            break
        if not isinstance(result,dict) or not result.get('ok'):
            raise runtime.RuntimeFault(str((result or {}).get('error','No verified runtime response')))
        if self.session and result.get('editor_session_id')!=self.session:
            self.report['replacement_editor_observation']=result
            raise runtime.RuntimeFault('Editor restarted; native ownership must be reconciled')
        native=result.get('network_session')
        if native and not native.get('ok') and op!='v2_state':
            raise runtime.RuntimeFault('NativeNetworkSession: '+str(native))
        return result

    def wait_ready(self,seconds):
        end=min(self.deadline,self.clock()+seconds)
        stable=None
        while True:
            result=self.call('v2_state')
            native=result['network_session']
            if not native.get('ok'): raise runtime.RuntimeFault('Native session is unavailable: '+str(native))
            if native['owned_pie_session_id']!=self.pie_session:
                raise runtime.RuntimeFault('Native PIE nonce changed')
            handles=tuple(sorted(w['world_handle'] for w in result['worlds'] if w['world_type']=='PIE'))
            if native['topology_ready'] and handles==stable:
                return result
            stable=handles if native['topology_ready'] else None
            if self.clock()>=end: raise TimeoutError('Native topology/map/controller/pawn readiness timed out')
            self.sleep(.25)

    def steps(self,steps):
        phase_end=min(self.deadline,self.clock()+self.spec['timeouts']['assert'])
        for step in steps:
            entry={'id':step['id'],'status':'running','attempts':0,'input_hash':runtime.digest(step)}
            self.report['steps'].append(entry); self.persist()
            end=min(phase_end,self.clock()+step.get('timeout_seconds',15))
            while True:
                snapshot=self.call('v2_state')
                if snapshot['pie_session_id']!=self.pie_session: raise runtime.RuntimeFault('PIE changed')
                world=runtime.select_world(snapshot,step['world']) if step.get('world') else None
                entry['attempts']+=1
                if step['type']=='input':
                    entry['result']=self.call('input',{'world_handle':world['world_handle'],'input_action_path':step['input_action_path'],'value':step['value']})
                    entry['status']='succeeded'; break
                field=step['field']
                if field=='world_count': actual=sum(w['world_type']=='PIE' for w in snapshot['worlds'])
                elif field in ('shader_jobs','asset_jobs'): actual=snapshot[field]
                else:
                    observation=self.call('observe',{'world_handle':world['world_handle']})
                    actual=observation[field]; entry['observation']=observation
                passed=runtime._compare(actual,step['value'],step.get('op','eq'))
                entry.update(actual=actual,expected=step['value'],passed=passed)
                if passed: entry['status']='succeeded'; break
                if step['type']=='assert': raise AssertionError(f"{step['id']}: condition failed")
                if self.clock()>=end: raise TimeoutError(f"{step['id']}: condition timeout")
                self.sleep(.25)
            self.persist()

    def execute(self):
        self.deadline=self.clock()+self.spec['timeout_seconds']
        self.report['status']='running'
        try:
            self.baseline=self.call('snapshot'); self.session=self.baseline['editor_session_id']
            self.report.update(baseline=self.baseline,editor_session_id=self.session)
            if self.baseline['pie'] or self.baseline.get('pie_session_id') or self.baseline['dirty_content'] or self.baseline['dirty_maps']:
                raise runtime.RuntimeFault('Clean unused Editor required; existing state is untouched')
            self.start_requested=True; self.report['start_requested']=True; self.persist()
            result=self.call('v2_start',dict(topology=self.spec['topology'],remote_client_count=self.spec['remote_client_count']-len(self.spec['join_plan']),**self.spec['network_profile']))
            self.pie_session=result['network_session']['owned_pie_session_id']
            if not self.pie_session: raise runtime.RuntimeFault('No owned start nonce')
            self.owned=True; self.report['pie_session_id']=self.pie_session
            self.report['initial_ready']=self.wait_ready(self.spec['timeouts']['start'])
            self.steps(self.spec['ready_conditions'])
            self.report['joins']=[]
            for item in self.spec['join_plan']:
                # Durable evidence precedes each single dispatch.
                self.report['joins'].append({'ordinal':item['client_ordinal'],'after_verified_steps':len(self.report['steps']),'status':'requesting'})
                self.persist()
                self.call('v2_join',{'remote_client_ordinal':item['client_ordinal']})
                result=self.wait_ready(self.spec['timeouts']['join'])
                self.report['joins'][-1].update(status='ready',evidence=result)
            self.steps(self.spec['steps'])
            self.report['ready']=self.call('v2_state')
            self.report['status']='succeeded'; self.report['recovery']='completed_no_retry'
            return {'success':True,'run_id':self.run_id,'report_path':str(self.path),'owned_pie_session_id':self.pie_session}
        except Exception as exc:
            self.report['status']='cancelled' if self.cancelled.is_set() else 'timed_out' if isinstance(exc,TimeoutError) else 'failed'
            self.report['error']=str(exc)
            raise
        finally: self.persist()

    def cleanup(self,state=None):
        try:
            self._cleanup_deadline=self.clock()+self.spec['timeouts']['cleanup']
            self.call('reconcile',cleanup=True)
            if self.unknown: raise runtime.RuntimeFault('Unconfirmed dispatch requires reconciliation')
            retain=self.spec['cleanup_policy']=='retain_for_feedback' and self.report['status']=='succeeded' and not self.cancelled.is_set()
            created=self.start_requested
            if self.start_requested:
                current=self.call('v2_state',cleanup=True)
                native=current['network_session']
                if native.get('error_code')=='UnknownSession' and not self.owned and not self.pie_session:
                    # Native registration precedes startup. Its absence proves
                    # this known failed dispatch never acquired a PIE session.
                    created=False; retain=False
                elif not native.get('ok'):
                    raise runtime.RuntimeFault('Native session needs reconciliation: '+str(native))
                self.pie_session=native.get('owned_pie_session_id',self.pie_session)
            if created:
                if native['status']=='needs_reconciliation': raise runtime.RuntimeFault('Native session ownership is uncertain')
                if native['status']!='cleaned' and not retain:
                    self.call('v2_stop',cleanup=True)
                    end=self._cleanup_deadline
                    while True:
                        current=self.call('v2_state',cleanup=True)
                        if current['network_session']['status']=='cleaned': break
                        if self.clock()>=end: raise TimeoutError('Owned session cleanup did not complete')
                        self.sleep(.25)
            final=self.call('diagnostics',cleanup=True)
            if self.baseline and (final['dirty_content']!=self.baseline['dirty_content'] or final['dirty_maps']!=self.baseline['dirty_maps']):
                raise runtime.RuntimeFault('Dirty changed; no save or restoration performed')
            if created and final['pie'] and not retain: raise runtime.RuntimeFault('PIE remains active')
            self.report['final']=final
            result={'success':True,'retained_for_feedback':retain,'run_id':self.run_id,'pie_session_id':self.pie_session,'assets_saved':[],
                    'side_effects_resolved':True,'cleanup_state':'awaiting_feedback' if retain else 'complete'}
        except Exception as exc:
            result={'success':False,'needs_reconciliation':True,'error':str(exc),'side_effects_resolved':False}
            self.report.update(status='needs_reconciliation',recovery='needs_reconciliation')
        self.report['cleanup']=result; self.persist(); return result
