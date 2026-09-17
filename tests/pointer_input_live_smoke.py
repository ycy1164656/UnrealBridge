"""Window-local coordinates and actual M-map mouse routing. No Content changes."""
import argparse
import json
import time
import uuid
from pathlib import Path
from network_multiclient_v3_live_smoke import _load_server


def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--project',required=True); parser.add_argument('--out',required=True)
    args=parser.parse_args(); path=Path(args.out); path.parent.mkdir(parents=True,exist_ok=True); server=_load_server()
    report={'status':'running','assertions':[],'sequences':[]}
    def persist(): path.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    def check(name,value,evidence=None):
        assert value,(name,evidence)
        report['assertions'].append(name); persist()
    run=server.network_sessions.NetworkSessionRun(dict(schema=server.network_sessions.SCHEMA,backend='owned_pie',topology='listen',remote_client_count=1,cleanup_policy='always'),
        lambda op,values,owner,cleanup:server._runtime_rpc(op,values,owner,cleanup,project=args.project),path.parent/'reports')
    def execute(code,world):
        response=server.bridge_submit_job(code,world_handle=world,project=args.project)
        assert response.get('job_id'),response
        result=server.bridge_wait_job(response['job_id'],wait_timeout=15,project=args.project)
        assert result.get('job_state')=='succeeded',result
        return server._last_json_output(result)
    def sequence(geometry,points,button='left',**changes):
        request={key:geometry[key] for key in ('world_handle','widget_handle','generation','window_id','geometry_revision','local_player_index')}
        request.update(schema='unrealbridge.pointer.v1',request_id=uuid.uuid4().hex,coordinate_space='widget_normalized',button=button,modifiers=[],events=points)
        request.update(changes)
        response=server.bridge_submit_pointer_action(request,project=args.project)
        assert response.get('job_id'),response
        result=server.bridge_wait_job(response['job_id'],wait_timeout=15,project=args.project)
        queued=server._last_json_output(result); assert queued, result
        if not queued.get('ok'): return queued
        end=time.monotonic()+20
        while True:
            state=execute('import unreal\nprint(unreal.UnrealBridgeSlateInputLibrary.get_pointer_sequence_state('+repr(queued['operation_id'])+'))',geometry['world_handle'])
            if state['status'] not in ('queued','running'): break
            if time.monotonic()>end: raise TimeoutError(state)
            time.sleep(.1)
        report['sequences'].append(state); persist(); return state
    try:
        run.execute(); run.deadline=time.monotonic()+300
        world=next(w for w in run.report['ready']['worlds'] if w['net_mode']=='ListenServer')
        view=execute('import json,unreal\n'+f"w=unreal.find_object(None,{world['world_path']!r})\n"+
            "hud=next(x for x in unreal.WidgetLibrary.get_all_widgets_of_class(w,unreal.SRGameHUDWidgetBase,False) if x.get_owning_player().is_local_controller())\n"+
            "hud.open_minimap()\n"+
            "m=next(x for x in unreal.WidgetLibrary.get_all_widgets_of_class(w,unreal.SRMinimapPanelWidgetBase,False) if x.get_owning_player()==hud.get_owning_player())\n"+
            "print(json.dumps({'hud':hud.get_path_name(),'map':m.get_path_name()}))",world['world_handle'])
        report['ui']=view; persist()
        def get_geometry():
            return execute('import unreal\nprint(unreal.UnrealBridgeSlateInputLibrary.get_widget_input_geometry('+repr(world['world_handle'])+','+repr(view['map'])+',0))',world['world_handle'])
        geometry=get_geometry(); previous=None; end=time.monotonic()+10
        while geometry.get('geometry_revision')!=previous:
            if time.monotonic()>end: raise TimeoutError('Map entrance geometry did not settle')
            previous=geometry.get('geometry_revision'); time.sleep(.2); geometry=get_geometry()
        report['geometry']=geometry; persist(); check('Live M-map geometry belongs to requested World',geometry.get('ok'),geometry)
        move=[dict(type='move',x=.5,y=.25,at_seconds=0)]
        moved=sequence(geometry,move); check('Native pointer move hit actual M-map',moved.get('status')=='succeeded',moved)
        check('Stale generation rejected',not sequence(geometry,move,generation='0'*32).get('ok'))
        check('Stale geometry revision rejected',not sequence(geometry,move,geometry_revision='0'*40).get('ok'))
        remote=next(w for w in run.report['ready']['worlds'] if w['net_mode']=='Client')
        check('Cross-world widget handle rejected',not sequence(geometry,move,world_handle=remote['world_handle']).get('ok'))
        geometry=get_geometry()
        report['click_geometry']=geometry; persist()
        # Actual NativeOnMouseButtonDown right-click closes the production map.
        clicked=sequence(geometry,[dict(type=k,x=.5,y=.25,at_seconds=i*.1) for i,k in enumerate(('move','down','up'))],button='right')
        # Once down closes the widget, the up is safely refused as stale. It
        # remains a runtime UI effect, not a blanket successful full sequence.
        closed=execute('import json,unreal\nm=unreal.find_object(None,'+repr(view['map'])+')\nprint(json.dumps({"visible":bool(m and m.is_visible())}))',world['world_handle'])
        check('Actual NativeOnMouseButtonDown closes M-map',not closed['visible'],clicked)
        check('No pressed state leaks when the target closes',not clicked.get('pressed'),clicked)
        report['status']='passed'
    except Exception as exc: report.update(status='failed',error=str(exc)); raise
    finally: report['cleanup']=run.cleanup(); persist()
    check('Exact owned PIE cleanup preserves Dirty',report['cleanup']['success'] and not run.report['final']['pie'],report['cleanup'])
    print(json.dumps({'status':report['status'],'assertions':len(report['assertions']),'out':str(path)}))


if __name__=='__main__': main()
