"""Real legacy recipe topology/ownership, Slate read API and MetaSound query.

This never mutates third-party assets or invokes rollback to test old APIs.
"""
import argparse
import json
from authoring_live_support import AuthoringLive


def main():
    p=argparse.ArgumentParser(); p.add_argument('--project',required=True); p.add_argument('--out',required=True)
    a=p.parse_args(); h=AuthoringLive(a.project,a.out); s=h.server; active=None
    def recipe(count=3,mode='owned_pie'):
        return dict(schema=s.runtime_recipes.SCHEMA,mode=mode,client_count=count,timeout_seconds=180,ready_timeout_seconds=90,
                    steps=[dict(id='topology',type='assert',field='world_count',value=count)])
    def run(spec):
        return s.runtime_recipes.RuntimeRun(spec,lambda op,values,owner,cleanup:s._runtime_rpc(op,values,owner,cleanup,project=h.project),h.path.parent/'reports')
    try:
        h.check('Clean Editor before compatibility verification',h.editor()=={'pie':False,'dirty':[]})
        meta=h.execute('import json,unreal\n'+
            "r=unreal.UnrealBridgeAudioLibrary.get_meta_sound_graph_info('/Game/Audio/MetaSounds/sfx_Random_nl_meta.sfx_Random_nl_meta')\n"+
            "print(json.dumps({'found':r.found,'path':r.path,'asset_class':r.asset_class,'nodes':r.total_node_count,'edges':r.total_edge_count,'error':r.error}))")
        h.report['metasound']=meta
        h.check('Existing MetaSound graph read API resolves a real graph',meta['found'] and meta['nodes']>0 and not meta['error'],meta)
        for action,args in [('Windows',dict(action='list')),('Snapshot',dict(ref='',maxDepth=1,bIncludeSourceLocations=False))]:
            result=s.bridge_submit_slate_action(action,args,project=h.project)
            if result.get('job_id'):
                result=s.bridge_wait_job(result['job_id'],wait_timeout=20,project=h.project)
            payload=s._last_json_output(result)
            h.report.setdefault('legacy_slate',[]).append(dict(action=action,envelope=result,payload=payload)); h.persist()
            h.check('Legacy Slate '+action+' executes on the native provider',result.get('success') and payload is not None,result)
        active=run(recipe()); h.check('Legacy v1 recipe starts its original topology',active.execute().get('success'))
        worlds=active.report['ready_worlds']
        h.check('v1 client_count=3 remains listen plus two clients',len(worlds)==3 and sorted(w['net_mode'] for w in worlds)==['Client','Client','ListenServer'],worlds)
        h.report['legacy_worlds']=worlds
        observer=run(recipe(mode='observe')); observer.execute(); kept=observer.cleanup()
        h.check('Legacy observer cleanup preserves an existing PIE',kept.get('success') and kept['final']['pie'] and kept['final']['pie_session_id']==active.pie_session,kept)
        stranger=run(recipe()); refused=False
        try: stranger.execute()
        except s.runtime_recipes.RuntimeFault as exc: refused='Existing PIE is user-owned' in str(exc)
        untouched=stranger.cleanup()
        h.check('A second owner refuses and cannot stop existing PIE',refused and untouched.get('success') and untouched['final']['pie_session_id']==active.pie_session,untouched)
        h.report['runtime_cleanup']=active.cleanup(); active=None
        h.check('Legacy owner releases only its own session',h.report['runtime_cleanup'].get('success'),h.report['runtime_cleanup'])
        h.report['scope']='Native legacy read APIs and v1 runtime ownership/topology; no legacy MetaSound asset mutation or visual acceptance claimed'
        h.finish()
    except Exception as exc:
        h.report.update(status='failed',error=str(exc)); h.persist(); raise
    finally:
        if active: h.report['runtime_cleanup']=active.cleanup(); h.persist()


if __name__=='__main__': main()
