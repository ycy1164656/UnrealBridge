"""Owns a v2 PIE lease and one explicit ShooterRoyal fixture for live probes."""
import json
import time
import uuid


class ScenarioLive:
    def __init__(self,h,scenario_id,lease_seconds=300):
        self.h=h; self.scenario_id=scenario_id; self.lease_seconds=lease_seconds; self.state=None; self.context=None
        s=h.server
        self.run=s.network_sessions.NetworkSessionRun(dict(schema=s.network_sessions.SCHEMA,backend='owned_pie',topology='listen',remote_client_count=1,cleanup_policy='always'),
            lambda op,values,owner,cleanup:s._runtime_rpc(op,values,owner,cleanup,project=h.project),h.path.parent/'reports')
    def __enter__(self):
        try:
            self.run.execute(); self.run.deadline=time.monotonic()+300
            self.world=next(w for w in self.run.report['ready']['worlds'] if w['net_mode']=='ListenServer')
            self.player=self.h.execute('import json,unreal\nfrom unreal_bridge import World\n'+f'w=unreal.find_object(None,{self.world["world_path"]!r})\n'+
                'p=next(p for p in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.PlayerController) if p.is_local_controller() and p.player_state and p.get_controlled_pawn())\n'+
                f'print(World.resolve_actor_reference(world_handle={self.world["world_handle"]!r},actor_name_or_path=p.get_path_name()))',self.world['world_handle'])
            self.context=self.request('context',set_enabled=True)
            self.state=self.request('prepare',world_generation=self.context['world_generation'],player_identity=self.context['player_identity'],scenario_id=self.scenario_id,seed=27606,lease_seconds=self.lease_seconds)
            self.h.check(self.scenario_id+' real fixture prepared',self.state.get('success'),self.state)
            return self
        except Exception:
            self.__exit__(None,None,None); raise
    def request(self,operation,**fields):
        request=dict(schema='shooterroyal.scenario.v1',operation=operation,request_id=uuid.uuid4().hex,world_handle=self.world['world_handle'],player_handle=self.player['actor_handle'],**fields)
        submitted=self.h.server.bridge_submit_sr_scenario(request,project=self.h.project)
        if not submitted.get('job_id'): raise RuntimeError(submitted)
        result=self.h.server.bridge_wait_job(submitted['job_id'],wait_timeout=15,project=self.h.project)
        if result.get('job_state')!='succeeded': raise RuntimeError(result)
        return self.h.server._last_json_output(result)
    def refresh(self):
        self.state=self.request('state',world_generation=self.context['world_generation'],run_id=self.state['run_id']); return self.state
    def action(self,action):
        return self.mutate('perform_scenario_action',action)
    def mutate(self,method,action=None):
        assert method in ('perform_scenario_action','release_scenario')
        # World callbacks can advance revision between host jobs. Keep read and dispatch atomic.
        code='import json,unreal\nfrom unreal_bridge import World\n'+f'r=json.loads(World.validate_actor_reference(actor_handle={self.player["actor_handle"]!r}))\n'+\
            f'assert r.get("ok") and r["world_handle"]=={self.world["world_handle"]!r}\n'+\
            'p=unreal.find_object(None,r["actor_path"]); s=unreal.SRAutomationScenarioSubsystem.get_for_player(p)\n'+\
            f'g={self.context["world_generation"]!r}; run={self.state["run_id"]!r}\n'+\
            'state=s.get_scenario_state(p,g,run)\n'+f'state=s.{method}(p,g,run,{uuid.uuid4().hex!r},state.revision'+(','+repr(action) if action else '')+')\n'+\
            "print(json.dumps({'success':state.success,'error':state.error,'error_code':state.error_code,'run_id':state.run_id,'revision':state.revision,'owned_actor_paths':list(state.owned_actor_paths),'cleanup_state':state.cleanup_state,'evidence':dict(state.evidence)}))"
        state=self.h.execute(code,self.world['world_handle'])
        if not state.get('success'): raise AssertionError((method,action,state))
        self.state=state; return state
    def __exit__(self,*args):
        try:
            if self.state and self.state.get('run_id'):
                end=self.mutate('release_scenario')
                self.h.report.setdefault('scenario_cleanup',[]).append(end)
                if not end.get('success'): self.h.report['scenario_cleanup_unresolved']=end
            if self.context: self.request('context',set_enabled=False)
        finally:
            if self.run.report.get('ready'):
                self.h.report['runtime_cleanup']=self.run.cleanup(); self.h.persist()
