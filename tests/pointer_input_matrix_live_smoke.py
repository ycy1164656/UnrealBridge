"""Real Slider capture and production M-map input under size/DPI changes."""
import argparse
import json
import time
import uuid
from authoring_live_support import AuthoringLive
from scenario_live_support import ScenarioLive


def main():
    p=argparse.ArgumentParser(); p.add_argument('--project',required=True); p.add_argument('--out',required=True); p.add_argument('--phase',choices=['all','interaction'],default='all'); a=p.parse_args(); h=AuthoringLive(a.project,a.out); h.report['phase']=a.phase
    try:
        h.check('Clean Editor baseline',h.editor()=={'pie':False,'dirty':[]})
        with ScenarioLive(h,'SRSC-TACTICAL-DEPLOY',lease_seconds=900) as scenario:
            world=scenario.world['world_handle']; scenario.run.deadline=time.monotonic()+600
            def execute(code): return h.execute(code,world)
            def call(fn,*args): return h.call('SlateInput',fn,*args,world=world)
            scenario.action('enter_experience')
            view=execute('import json,unreal\n'+f'w=unreal.find_object(None,{scenario.world["world_path"]!r})\n'+
                'hud=next(x for x in unreal.WidgetLibrary.get_all_widgets_of_class(w,unreal.SRGameHUDWidgetBase,False) if x.get_owning_player().is_local_controller())\n'+
                'hud.close_minimap(); hud.invalidate_player_context(); hud.resolve_player_context(); hud.open_minimap()\n'+
                'm=next(x for x in unreal.WidgetLibrary.get_all_widgets_of_class(w,unreal.SRMinimapPanelWidgetBase,False) if x.get_owning_player()==hud.get_owning_player())\n'+
                "m.invalidate_player_context(); m.resolve_player_context(); m.set_active_map_tab('Towers')\n"+
                'print(json.dumps({"hud":hud.get_path_name(),"map":m.get_path_name(),"player":hud.get_owning_player().get_path_name(),"cached_state":m.get_cached_sr_player_state().get_path_name()}))')
            h.report['view']=view; h.check('Map uses only the prepared player state',view['cached_state']==scenario.state['evidence']['fixture_player_state'],view)
            def widget(name):
                return execute('import json,unreal\n'+f'items=unreal.UnrealBridgeUMGLibrary.get_runtime_widget_tree("",{view["map"]!r})\n'+
                    f'matches=[x.widget_object_path for x in items if x.owner_user_widget_path=={view["map"]!r} and x.name=={name!r}]\n'+
                    'assert len(matches)<=1,matches\nprint(json.dumps({"path":matches[0] if matches else None}))')['path']
            def geom(path):
                previous=None; deadline=time.monotonic()+8
                while True:
                    g=call('get_widget_input_geometry',world,path,0)
                    if g.get('ok') and previous==g['geometry_revision']: return g
                    if time.monotonic()>deadline: raise AssertionError(('Geometry did not settle',g))
                    previous=g.get('geometry_revision'); time.sleep(.15)
            def submit(g,events,button='left',**changes):
                req={k:g[k] for k in ('world_handle','widget_handle','generation','window_id','geometry_revision','local_player_index')}
                req.update(schema='unrealbridge.pointer.v1',request_id=uuid.uuid4().hex,coordinate_space='widget_normalized',button=button,modifiers=[],events=events); req.update(changes)
                job=h.server.bridge_submit_pointer_action(req,project=h.project)
                if not job.get('job_id'):
                    h.report.setdefault('submissions',[]).append(job); h.persist(); return job
                result=h.server.bridge_wait_job(job['job_id'],wait_timeout=15,project=h.project)
                value=h.server._last_json_output(result)
                h.report.setdefault('submissions',[]).append(value or result); h.persist()
                return value or result
            def wait(q):
                if not q.get('operation_id'): return q
                deadline=time.monotonic()+15
                while True:
                    state=call('get_pointer_sequence_state',q['operation_id'])
                    if state.get('status') not in ('running','queued'): break
                    if time.monotonic()>deadline: raise TimeoutError(state)
                    time.sleep(.1)
                h.report.setdefault('sequences',[]).append(state); h.persist(); return state
            def events(x,y): return [dict(type=t,x=x,y=y,at_seconds=i*.08) for i,t in enumerate(('move','down','up'))]
            def click(path,x=.5,y=.5,button='left'):
                if path==card:
                    # The production card intentionally has no hit-testable
                    # leaf. Its root receives/bubbles the real click; derive
                    # that receiver coordinate from both live geometries.
                    point=execute('import json,unreal\n'+f'c=unreal.find_object(None,{card!r}); r=unreal.find_object(None,{root!r})\n'+
                        'cg=c.get_cached_geometry(); rg=r.get_cached_geometry(); lib=unreal.SlateLibrary\ncs=lib.get_local_size(cg); rs=lib.get_local_size(rg)\n'+
                        f'p=lib.absolute_to_local(rg,lib.local_to_absolute(cg,unreal.Vector2D(cs.x*{x!r},cs.y*{y!r})))\n'+
                        'print(json.dumps({"x":p.x/rs.x,"y":p.y/rs.y,"anchor":c.get_path_name(),"receiver":r.get_path_name(),"visibility":str(c.get_visibility())}))')
                    h.report.setdefault('card_input_mapping',[]).append(point); path=root; x=point['x']; y=point['y']
                for attempt in range(4):
                    result=wait(submit(geom(path),events(x,y),button))
                    # A stale pre-dispatch reference has sent no button event.
                    # Re-read it; never repeat a partly executed down/up.
                    if result.get('operation_id') or result.get('error_code')!='StaleGeometry': return result
                return result
            def map_state():
                return execute('import json,unreal\nm=unreal.find_object(None,'+repr(view['map'])+')\nr=m.get_tactical_map_deployment_result(); p=m.get_editor_property("pending_deployment_request")\n'+
                    'print(json.dumps({"state":str(r.state),"preview":r.state==unreal.SRTacticalMapDeploymentState.PREVIEW,"success":r.success,"duplicate":r.was_duplicate_request,"failure":str(r.failure),"x":p.map_position.x,"y":p.map_position.y,"request_id":str(p.request_id),"world":str(r.authoritative_world_location)}))')
            stage=widget('Canvas_MapStage'); root=view['map']; card_index=int(scenario.state['evidence']['deployment_card_index']); card=widget('Border_MapTowerSlot_'+str(card_index))
            h.check('Production map stage and actual building card exist',bool(stage and card),dict(stage=stage,card=card))
            def resize_viewport(width,height,dpi):
                w=call('get_owned_pie_window_geometry',scenario.run.run_id,world)
                h.check('Native lease selects its own PIE window',w.get('ok'),w)
                client_width,client_height=width,height
                for attempt in range(4):
                    req=dict(schema='unrealbridge.pie_window.v1',request_id=uuid.uuid4().hex,run_id=scenario.run.run_id,world_handle=world,window_id=w['window_id'],width_px=client_width,height_px=client_height,dpi_scale=dpi)
                    resized=call('set_owned_pie_window_geometry',json.dumps(req)); h.check('Bounded window layout request accepted',resized.get('ok'),resized)
                    deadline=time.monotonic()+8; previous=None
                    while True:
                        actual=call('get_owned_pie_window_geometry',scenario.run.run_id,world)
                        measured=tuple(actual.get(k) for k in ('viewport_width_px','viewport_height_px','dpi_scale'))
                        if measured==previous: break
                        if time.monotonic()>deadline: raise AssertionError(('Viewport layout did not settle',actual,req))
                        previous=measured; time.sleep(.15)
                    if measured==(width,height,dpi): return actual
                    # Native client size and the rendered viewport differ with
                    # custom title/border metrics. Correct from actual readback,
                    # rather than relabeling a 1082-pixel viewport as 1080.
                    h.report.setdefault('window_calibration',[]).append(dict(request=req,actual=actual)); h.persist()
                    client_width=round(client_width+width-actual['viewport_width_px'])
                    client_height=round(client_height+height-actual['viewport_height_px'])
                raise AssertionError(('Requested real viewport layout not reached',actual,req))
            old=None
            # Read actual viewport sizes after the native owned-window request.
            for width,height,dpi in ([(1920,1080,1),(1280,720,1.25),(1920,1080,1.25),(1280,720,1)] if a.phase=='all' else []):
                actual=resize_viewport(width,height,dpi)
                h.report.setdefault('layouts',[]).append(actual)
                current=geom(stage)
                if old: h.check('Old geometry refuses after actual layout change',not submit(old,[dict(type='move',x=.5,y=.5,at_seconds=0)]).get('ok'))
                moved=wait(submit(current,[dict(type='move',x=.5,y=.5,at_seconds=0)]))
                h.check(f'{width}x{height} at DPI {dpi} routes through actual geometry',moved.get('status')=='succeeded',moved)
                old=current
            if a.phase=='interaction': resize_viewport(1280,720,1)
            # Separate real slider surface: construction never calls OnValueChanged.
            probe=execute('import unreal,json\np=unreal.find_object(None,'+repr(view['player'])+')\nu=unreal.SRInputAutomationProbeWidget.create_input_probe(p)\nprint(json.dumps({"path":u.get_path_name() if u else None}))')['path']
            h.check('Transient owned slider surface created',bool(probe))
            def probe_state(): return execute('import unreal,json\nprint(json.dumps(json.loads(unreal.find_object(None,'+repr(probe)+').get_probe_observation())))')
            try:
                initial=probe_state(); slider=initial['slider_widget_path']; g=geom(slider)
                drag=[dict(type='move',x=.2,y=.5,at_seconds=0),dict(type='down',x=.2,y=.5,at_seconds=.1),dict(type='move',x=.8,y=.5,at_seconds=.3),dict(type='up',x=.8,y=.5,at_seconds=.5)]
                result=wait(submit(g,drag)); observed=probe_state()
                h.check('Actual Slider drag changes value and releases capture',result.get('status')=='succeeded' and observed['value']>.7 and observed['value_changes']>0 and observed['capture_begins']==observed['capture_ends']==1,dict(result=result,probe=observed))
                g=geom(slider); slow=drag[:-1]+[dict(type='up',x=.8,y=.5,at_seconds=7)]
                queued=submit(g,slow); time.sleep(.35); cancelled=call('cancel_pointer_sequence',queued['operation_id'],world); result=wait(queued); observed=probe_state()
                h.check('Cancel during held drag releases only its own capture',result.get('status')=='cancelled' and not result['pressed'] and observed['capture_begins']==observed['capture_ends'],dict(state=result,probe=observed,cancel=cancelled))
                execute('import unreal,json\nu=unreal.find_object(None,'+repr(probe)+')\nu.set_probe_obscured(True)\nprint(json.dumps({"ok":True}))'); time.sleep(.15)
                before=probe_state(); blocked=wait(submit(geom(slider),events(.5,.5))); after=probe_state()
                h.check('Obscured control is refused and never clicked',blocked.get('status')=='failed' and blocked.get('error')=='ClippedOrOccluded' and after['value_changes']==before['value_changes'],blocked)
            finally:
                execute('import unreal,json\nu=unreal.find_object(None,'+repr(probe)+')\nu.remove_from_parent()\nprint(json.dumps({"removed":True}))')
            # Preview coordinates originate exclusively from Slate down events.
            for x,y in [(.5,.5),(.01,.5),(.99,.5),(.5,.01),(.5,.99)]:
                selected=click(card)
                if selected.get('status')!='succeeded':
                    h.report['card_diagnostic']=dict(geometry=geom(card),capture=call('capture_owned_pie_window',scenario.run.run_id,world),
                        widgets=execute('import json,unreal\nu=unreal.find_object(None,'+repr(card)+')\nitems=[]\nwhile u:\n items.append(dict(path=u.get_path_name(),visibility=str(u.get_visibility()),enabled=u.get_is_enabled())); u=u.get_parent()\nprint(json.dumps({"ancestors":items}))'))
                    h.persist()
                h.check('Building card receives real complete pointer sequence',selected.get('status')=='succeeded',selected)
                result=click(stage,x,y); preview=map_state()
                h.check('M map preview matches native normalized click',preview['preview'] and abs(preview['x']-x)<.003 and abs(preview['y']-y)<.003,dict(preview=preview,sequence=result))
                click(stage,x,y,'right'); cancelled=map_state()
                h.check('Right button cancels preview without deployment',not cancelled['preview'] and not cancelled['success'],cancelled)
            e=scenario.refresh()['evidence']; before=float(e['deployment_gold']); x=float(e['legal_map_x']); y=float(e['legal_map_y'])
            click(card); first=click(stage,x,y); preview=map_state(); h.check('First legal click only previews',preview['preview'] and float(scenario.refresh()['evidence']['deployment_gold'])==before,preview)
            second=click(stage,x,y); deadline=time.monotonic()+12
            while True:
                result=map_state(); evidence=scenario.action('observe_deployment')['evidence']
                if result['success'] and evidence.get('deployed_building'): break
                if time.monotonic()>deadline: raise AssertionError(('Real second-click deployment did not succeed',result,evidence,second))
                time.sleep(.2)
            h.report['deployment']=dict(result=result,evidence=evidence,first=first,second=second)
            h.check('Two real Slate clicks create one server-owned building',result['success'] and bool(evidence['deployed_building']),result)
            h.check('Authority charges the exact registered cost once',abs(before-float(evidence['deployment_gold'])-float(evidence['deployment_cost']))<.01,evidence)
            after=float(evidence['deployment_gold']); duplicate=scenario.action('repeat_last_deployment')['evidence']
            h.check('Business duplicate replays receipt without second fee',duplicate.get('deployment_duplicate')=='true' and float(duplicate['deployment_gold'])==after,duplicate)
            invalid=scenario.action('invalid_bounds_deployment')['evidence']; h.check('Out-of-bounds business request cannot charge',invalid.get('deployment_success')=='false' and float(invalid['deployment_gold'])==after,invalid)
            resize_viewport(1920,1080,1)
            geom(stage)
            capture=call('capture_owned_pie_window',scenario.run.run_id,world); h.report['screenshot']=capture
            h.check('Actual Slate UI captured at 1920x1080',capture.get('ok') and capture['width']==1920 and capture['height']==1080 and capture['includes_slate_ui'],capture)
            execute('import unreal,json\nunreal.find_object(None,'+repr(view['hud'])+').close_minimap()\nprint(json.dumps({"closed":True}))')
        h.finish()
    except Exception as exc:
        h.report.update(status='failed',error=str(exc)); h.persist(); raise


if __name__=='__main__': main()
