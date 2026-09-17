"""Real remote GAS prediction rejection, fixture handoff and replicated readback."""
import argparse
import json
import time
from pathlib import Path
from network_multiclient_v3_live_smoke import _load_server


def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--project',required=True); parser.add_argument('--out',required=True)
    args=parser.parse_args(); server=_load_server(); path=Path(args.out); path.parent.mkdir(parents=True,exist_ok=True)
    report={'status':'running','assertions':[],'repetitions':[]}
    def persist(): path.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    def check(name,value,evidence=None):
        assert value,(name,evidence)
        report['assertions'].append(name); persist()
    spec=dict(schema=server.network_sessions.SCHEMA,backend='owned_pie',topology='listen',remote_client_count=1,
              cleanup_policy='always',network_profile={'out_lag_ms':100,'out_loss_percent':0})
    run=server.network_sessions.NetworkSessionRun(spec,lambda op,values,owner,cleanup:server._runtime_rpc(op,values,owner,cleanup,project=args.project),path.parent/'reports')
    report['network_report']=str(run.path)
    authority_context=None
    def execute(code,world):
        response=server.bridge_submit_job(code,world_handle=world,project=args.project)
        assert response.get('job_id'),response
        result=server.bridge_wait_job(response['job_id'],wait_timeout=15,project=args.project)
        assert result.get('job_state')=='succeeded',result
        value=server._last_json_output(result); assert value is not None,result
        return value
    serial=0
    def scenario(world,player,operation,**fields):
        nonlocal serial
        serial+=1
        request=dict(schema='shooterroyal.scenario.v1',operation=operation,world_handle=world['world_handle'],player_handle=player['actor_handle'],request_id=f'{run.run_id}-{serial}',**fields)
        response=server.bridge_submit_sr_scenario(request,project=args.project)
        assert response.get('job_id'),response
        result=server.bridge_wait_job(response['job_id'],wait_timeout=15,project=args.project)
        assert result.get('job_state')=='succeeded',result
        return server._last_json_output(result)
    def resolve(world,expression):
        return execute("import json,unreal\nfrom unreal_bridge import World\n"+f"w=unreal.find_object(None,{world['world_path']!r})\n"+
            "players=unreal.GameplayStatics.get_all_actors_of_class(w,unreal.PlayerController)\n"+
            f"p=next(p for p in players if {expression})\nprint(World.resolve_actor_reference(world_handle={world['world_handle']!r},actor_name_or_path=p.get_path_name()))",world['world_handle'])
    def view(world,player):
        return execute("import json,unreal\nfrom unreal_bridge import World\n"+
            f"ref=json.loads(World.validate_actor_reference(actor_handle={player['actor_handle']!r}));p=unreal.find_object(None,ref['actor_path'])\n"+
            "pawn=p.get_controlled_pawn(); state=p.player_state; asc=unreal.AbilitySystemLibrary.get_ability_system_component(state)\n"+
            "print(json.dumps({'pawn':pawn.get_path_name() if pawn else '', 'state':state.get_path_name() if state else '', 'asc':asc.get_path_name() if asc else '', 'pawn_state':pawn.player_state.get_path_name() if pawn and pawn.player_state else '', 'state_identity':dict(unreal.SRAutomationFixtureIdentityComponent.read_fixture_identity(state)), 'pawn_identity':dict(unreal.SRAutomationFixtureIdentityComponent.read_fixture_identity(pawn))}))",world['world_handle'])
    try:
        run.execute()
        # The recipe execution budget is separate from this bounded gameplay phase.
        run.deadline=time.monotonic()+600
        worlds=run.report['ready']['worlds']; authority=next(w for w in worlds if w['net_mode']=='ListenServer'); client=next(w for w in worlds if w['net_mode']=='Client')
        host_player=resolve(authority,'not p.is_local_controller()')
        client_player=resolve(client,'p.is_local_controller()')
        original=view(client,client_player); report['original_client']=original
        authority_context=scenario(authority,host_player,'context',set_enabled=True)
        client_context=scenario(client,client_player,'context')
        for repetition in range(3):
            state=scenario(authority,host_player,'prepare',world_generation=authority_context['world_generation'],player_identity=authority_context['player_identity'],scenario_id='SRSC-PREDICTION-CANCEL',seed=27603)
            check(f'Prepared real LocalPredicted weapon {repetition}',state['success'],state)
            scope=dict(world_generation=authority_context['world_generation'],run_id=state['run_id'])
            client_scope=dict(world_generation=client_context['world_generation'],run_id=state['run_id'])
            entry={'prepared':state,'actions':[]}; report['repetitions'].append(entry); persist()
            def action(name):
                current=scenario(authority,host_player,'state',**scope)
                value=scenario(authority,host_player,'action',**scope,expected_revision=current['revision'],action=name)
                entry['actions'].append({'action':name,'state':value}); persist()
                check(f'{name} {repetition}',value['success'],value)
                return value
            entered=action('enter_experience')
            end=time.monotonic()+40
            while True:
                current=view(client,client_player)
                if current['state_identity'].get('run_id')==state['run_id'] and current['pawn_identity'].get('run_id')==state['run_id'] and current['pawn_state']==current['state'] and current['asc']: break
                if time.monotonic()>=end: raise TimeoutError(f'Fixture client replication did not settle: {current}')
                time.sleep(.25)
            entry['entered_client']=current; persist()
            observed=scenario(client,client_player,'prediction_begin',**client_scope)
            check(f'Native client observer started {repetition}',observed.get('ok')=='true',observed)
            entry['observer_before']=observed
            action('arm_rejection')
            # Queue the actual bound weapon input for native World ticks. Python's
            # editor script guard forces RPCs local, so it must never activate GAS directly.
            attempt=scenario(client,client_player,'prediction_input',**client_scope)
            entry['activation_attempt']=attempt
            entry['observer_after_attempt']=scenario(client,client_player,'prediction_state',**client_scope)
            persist()
            check(f'Client queued real weapon input {repetition}',attempt.get('ok')=='true' and attempt.get('input_status')=='queued',attempt)
            end=time.monotonic()+30
            while True:
                observed=scenario(client,client_player,'prediction_state',**client_scope)
                entry['observer_after']=observed; persist()
                if int(observed.get('rejected_keys','0'))>0 and int(observed.get('now_active_weapon_abilities','-1'))==0: break
                if time.monotonic()>=end: raise TimeoutError(f'Real rejection did not settle: {observed}')
                time.sleep(.25)
            check(f'Real matching prediction key rejection {repetition}',observed['prediction_key']==observed['rejected_prediction_key'] and int(observed['predicted_activations'])==1 and int(observed['rejected_keys'])==1,observed)
            action('check_rejection'); action('clear_rejection')
            for field in ('magazine_ammo','gold','active_effects','active_cue_tags','input_pressed_count','active_weapon_abilities'):
                check(f'Client {field} converged {repetition}',observed['before_'+field]==observed['now_'+field],observed)
            check(f'Cooldown converged {repetition}',float(observed['now_cooldown_seconds'])<=float(observed['before_cooldown_seconds'])+.01,observed)
            scenario(client,client_player,'prediction_end',**client_scope)
            action('leave_experience')
            end=time.monotonic()+30
            while view(client,client_player)!=original:
                if time.monotonic()>=end: raise TimeoutError('Original client possession not restored')
                time.sleep(.25)
            check(f'Original unmodified client possession returned {repetition}',True)
            state=scenario(authority,host_player,'state',**scope)
            released=scenario(authority,host_player,'release',**scope,expected_revision=state['revision'])
            check(f'Owned fixture released {repetition}',released['success'] and not released['owned_actor_paths'],released)
        scenario(authority,host_player,'context',set_enabled=False)
        report['status']='passed'
    except Exception as exc:
        report.update(status='failed',error=str(exc)); raise
    finally:
        if authority_context and authority_context.get('enabled'):
            try: scenario(authority,host_player,'context',set_enabled=False)
            except Exception as exc: report['opt_in_cleanup_error']=str(exc)
        report['cleanup']=run.cleanup(); persist()
    check('Exact PIE cleaned with unchanged Dirty',report['cleanup']['success'] and not run.report['final']['pie'],report['cleanup'])
    print(json.dumps({'status':report['status'],'assertions':len(report['assertions']),'out':str(path)}))


if __name__=='__main__': main()
