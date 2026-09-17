"""Real PIE/world-handle/stdio-host adapter check. Owns only the PIE it starts."""
import argparse
import json
import threading
import time
import uuid
from pathlib import Path
from types import SimpleNamespace
from network_multiclient_v3_live_smoke import _load_server


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--project', required=True)
    parser.add_argument('--out', required=True)
    parser.add_argument('--gameplay', action='store_true')
    parser.add_argument('--scenario', default='')
    args = parser.parse_args()
    server = _load_server()
    output = Path(args.out)
    report = {'assertions': [], 'jobs': [], 'status': 'running'}
    def persist():
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    owner = SimpleNamespace(run_id='srub02-'+uuid.uuid4().hex, session=None,
        pie_session=None, report=report, persist=persist, unknown=False,
        deadline=time.monotonic()+240, cancelled=threading.Event())
    def rpc(op, values=None, cleanup=False):
        deadline=time.monotonic()+25
        while True:
            value=server._runtime_rpc(op, values or {}, owner, cleanup, project=args.project)
            if isinstance(value,dict) and 'pie' in value: return value
            if op!='snapshot' or time.monotonic()>=deadline:
                raise RuntimeError(f'Incomplete runtime {op} response: {value}')
            report.setdefault('snapshot_retries',[]).append(value)
            time.sleep(.5)
    def execute(code, world=None):
        submitted = server.bridge_submit_job(code, world_handle=world, project=args.project)
        assert submitted.get('job_id'), submitted
        result = server.bridge_wait_job(submitted['job_id'], wait_timeout=15, project=args.project)
        assert result.get('job_state') == 'succeeded', result
        value = server._last_json_output(result)
        assert isinstance(value, dict), result
        return value
    def check(name, condition, data=None):
        assert condition, (name, data)
        report['assertions'].append(name)
        persist()
    count = 0
    def scenario(op, **fields):
        nonlocal count
        count += 1
        request = dict(schema='shooterroyal.scenario.v1',operation=op,
            request_id=f'{owner.run_id}-{count}',world_handle=world['world_handle'],
            player_handle=player['actor_handle'],**fields)
        submitted = server.bridge_submit_sr_scenario(request, project=args.project)
        assert submitted.get('job_id'), submitted
        response = server.bridge_wait_job(submitted['job_id'], wait_timeout=15, project=args.project)
        assert response.get('terminal') and response.get('job_state') == 'succeeded', response
        value = server._last_json_output(response)
        assert value is not None, response
        return value, request, submitted
    started = False
    context = None
    try:
        report['before'] = rpc('snapshot')
        owner.session = report['before']['editor_session_id']
        check('Clean unused PIE baseline', not report['before']['pie'] and not report['before']['dirty_content'] and not report['before']['dirty_maps'])
        initial = rpc('start', {'client_count':1})
        owner.pie_session = initial['pie_session_id']
        started = True
        check('PIE ownership recorded', initial['lease_owner'] == owner.run_id and bool(owner.pie_session))
        deadline = time.monotonic()+90
        player = None
        while time.monotonic()<deadline:
            current = rpc('snapshot')
            worlds = [w for w in current['worlds'] if w['world_type']=='PIE' and w['net_mode']!='Client' and w['has_begun_play']]
            if len(worlds)==1:
                world=worlds[0]
                player=execute("import json,unreal\nfrom unreal_bridge import World\n"
                    f"w=unreal.find_object(None,{world['world_path']!r})\n"
                    "players=[p for p in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.PlayerController) if p.player_state and p.get_controlled_pawn()]\n"
                    "print(World.resolve_actor_reference(world_handle="+repr(world['world_handle'])+",actor_name_or_path=players[0].get_path_name()) if len(players)==1 else json.dumps({'waiting':True}))",world['world_handle'])
                if player.get('actor_handle'): break
            time.sleep(.5)
        check('One exact authority player with pawn', bool(player and player.get('actor_handle')), player)
        report['world'],report['player']=world,player
        context,_,_=scenario('context')
        check('Scenario gate initially disabled', not context['enabled'],context)
        context,_,_=scenario('context',set_enabled=True)
        check('Explicit runtime opt-in accepted',context['enabled'],context)
        state,request,job=scenario('prepare',world_generation=context['world_generation'],player_identity=context['player_identity'],scenario_id='SRSC-FRAMEWORK-PROBE')
        check('Live native prepare creates one transient actor',state['success'] and len(state['owned_actor_paths'])==1,state)
        again=server.bridge_submit_sr_scenario(request,project=args.project)
        check('Host repeat has same durable job',again.get('deduplicated') and again.get('job_id')==job['job_id'],again)
        scope={'world_generation':context['world_generation'],'run_id':state['run_id']}
        state,_,_=scenario('reset',**scope,expected_revision=state['revision'])
        check('Reset reconstructs only owned probe',state['success'] and len(state['owned_actor_paths'])==1,state)
        state,_,_=scenario('takeover',**scope,expected_revision=state['revision'])
        check('User handoff retains fixture',state['success'] and state['status']=='awaiting_feedback',state)
        state,_,_=scenario('release',**scope,expected_revision=state['revision'])
        check('Explicit release verified',state['success'] and not state['owned_actor_paths'] and state['cleanup_state']=='complete',state)
        report['final_state']=state
        if args.gameplay:
            plans = {
                'SRSC-INVENTORY-FULL':['pickup','free_slot','pickup'],
                'SRSC-CLASS-LEGAL':['select_forbidden','select_legal'],
                'SRSC-TALENT-LV3':['upgrade','exhaust_and_reject'],
                'SRSC-SERIES-2-3':['equip_one','equip_two','equip_three','remove_third'],
                'SRSC-CRITICAL-HIT':['normal_hit','check_normal_hit','critical_hit','check_critical_hit'],
                'SRSC-INJURED-ALLY':['start_healing','check_healing'],
                'SRSC-ECONOMY-PITY':['insufficient_purchase','purchase_once','guarantee_batch'],
            }
            if args.scenario:
                plans = {args.scenario:plans[args.scenario]}
            report['gameplay_runs']=[]
            for scenario_id,actions in plans.items():
                signatures=[]
                for repetition in range(3):
                    state,_,_=scenario('prepare',world_generation=context['world_generation'],player_identity=context['player_identity'],scenario_id=scenario_id,seed=27603)
                    entry={'scenario_id':scenario_id,'repeat':repetition,'prepared':state,'actions':[]}
                    report['gameplay_runs'].append(entry)
                    check(f'{scenario_id} prepare {repetition}',state['success'],state)
                    scope={'world_generation':context['world_generation'],'run_id':state['run_id']}
                    for action in actions:
                        if action=='check_healing': time.sleep(1)
                        # The native damage callback can advance revision between jobs.
                        state,_,_=scenario('state',**scope)
                        state,_,_=scenario('action',**scope,expected_revision=state['revision'],action=action)
                        entry['actions'].append({'action':action,'state':state})
                        check(f'{scenario_id} {action} {repetition}',state['success'],state)
                    stable={k:v for k,v in state['evidence'].items() if k not in ('fixture_player_state','fixture_character','experience_entered') and not k.endswith('_instance')}
                    signatures.append(stable)
                    state,_,_=scenario('release',**scope,expected_revision=state['revision'])
                    entry['released']=state
                    check(f'{scenario_id} release {repetition}',state['success'] and not state['owned_actor_paths'],state)
                check(f'{scenario_id} same seed repeats three times',signatures[0]==signatures[1]==signatures[2],signatures)
        report['status']='passed'
    except Exception as exc:
        report.update(status='failed',error=str(exc))
        raise
    finally:
        if started:
            current=rpc('snapshot',cleanup=True)
            if current.get('lease_owner')==owner.run_id and current.get('pie_session_id')==owner.pie_session:
                if context and context.get('enabled'):
                    scenario('context',set_enabled=False)
                rpc('stop',cleanup=True)
                deadline=time.monotonic()+30
                while time.monotonic()<deadline:
                    current=rpc('snapshot',cleanup=True)
                    if not current['pie'] and not current['pie_session_id']: break
                    time.sleep(.25)
                rpc('release',cleanup=True)
        report['after']=rpc('snapshot',cleanup=True)
        persist()
    check('Final PIE stopped and Dirty unchanged', not report['after']['pie'] and report['after']['dirty_content']==report['before']['dirty_content'] and report['after']['dirty_maps']==report['before']['dirty_maps'])
    print(json.dumps({'status':report['status'],'assertions':len(report['assertions']),'out':str(output)}))


if __name__=='__main__':
    main()
