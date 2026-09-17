"""Shared live authoring evidence harness. Saves only explicitly named packages."""
import json
import time
import uuid
from pathlib import Path
from network_multiclient_v3_live_smoke import _load_server


class AuthoringLive:
    def __init__(self, project, output):
        self.project=project; self.path=Path(output); self.path.parent.mkdir(parents=True,exist_ok=True)
        self.server=_load_server(); self.report={'status':'running','assertions':[],'operations':[]}
    def persist(self): self.path.write_text(json.dumps(self.report,ensure_ascii=False,indent=2),encoding='utf-8')
    def check(self,name,condition,evidence=None):
        if not condition: raise AssertionError((name,evidence))
        self.report['assertions'].append(name); self.persist()
    def execute(self,code,world=None):
        job=self.server.bridge_submit_job(code,world_handle=world,project=self.project)
        if not job.get('job_id'): raise RuntimeError(job)
        deadline=time.monotonic()+40
        while time.monotonic()<deadline:
            result=self.server.bridge_wait_job(job['job_id'],wait_timeout=10,project=self.project)
            if result.get('terminal'): break
        if result.get('job_state')!='succeeded': raise RuntimeError(result)
        value=self.server._last_json_output(result)
        if value is None: raise RuntimeError(result)
        return value
    def call(self,library,function,*args,world=None):
        return self.execute('import unreal\nprint(unreal.UnrealBridge'+library+'Library.'+function+'('+','.join(repr(x) for x in args)+'))',world)
    def editor(self):
        value=self.execute("import json,unreal,unreal_bridge\ns=unreal.UnrealBridgeEditorLibrary.get_editor_state()\n"+
            "print(json.dumps({'pie':s.is_pie,'dirty':sorted(unreal.UnrealBridgeEditorLibrary.get_dirty_package_names()),'environment':{"+
            "'plugin_version':unreal_bridge._PLUGIN_VERSION,'manifest_hash':unreal_bridge._MANIFEST_HASH,'engine_version':unreal.SystemLibrary.get_engine_version(),"+
            "'render_offscreen':'-renderoffscreen' in unreal.SystemLibrary.get_command_line().lower(),'no_sound':'-nosound' in unreal.SystemLibrary.get_command_line().lower()}}))")
        self.report['environment']=value.pop('environment'); self.persist(); return value
    def snapshot(self,target): return self.call('Upgrade','get_authoring_snapshot',json.dumps(target if isinstance(target,list) else [target]))
    def request(self,target,operation,ops,dry=True,save=False):
        snapshot=self.snapshot(target)
        context={k:snapshot[k] for k in ('schema','project_identity','editor_session_id','engine_version')}
        targets=target if isinstance(target,list) else [target]
        context.update(request_id='author-'+uuid.uuid4().hex,operation_id='upgrade.validate',target_packages=targets,
            expected_revisions={p:snapshot['targets'][p]['revision'] for p in targets},dry_run=dry,save_policy='never',timeout_seconds=60)
        return dict(schema='unrealbridge.authoring.v1',operation_id=operation,context=context,operations=ops,save_policy='declared_targets' if save else 'never')
    def author(self,request):
        submitted=self.server.bridge_submit_authoring_request(request,project=self.project)
        if not submitted.get('job_id'): return submitted
        result=self.server.bridge_wait_job(submitted['job_id'],wait_timeout=20,project=self.project)
        if result.get('job_state')!='succeeded': raise RuntimeError(result)
        value=self.server._last_json_output(result)
        self.report['operations'].append({'request_id':request['context']['request_id'],'operation_id':request['operation_id'],'result':value})
        self.persist(); return value
    def finish(self):
        self.report['after']=self.editor(); self.check('Final Editor clean and PIE stopped',self.report['after']=={'pie':False,'dirty':[]},self.report['after'])
        self.report['status']='passed'; self.persist(); print(json.dumps({'status':'passed','checks':len(self.report['assertions']),'out':str(self.path)}))
