"""Read-only registered project evidence; fixed contracts and actual saved-view proof."""
import hashlib
import importlib
import json
from pathlib import Path
import re
import sys
import unreal


def collect(recipe, plan_hash, adapter_id, source_hash, run_id, world_handle):
    from unreal_bridge_content_ops import _content_hash, _content_snapshot
    if _content_hash(recipe)!=plan_hash:raise ValueError('Frozen plan changed')
    project=Path(unreal.Paths.get_project_file_path()).resolve()
    if str(project).replace('\\','/').casefold()!=recipe['project_identity'].casefold():raise ValueError('Wrong project')
    registry=project.parent/'Tools/UnrealBridge/verification_adapters.json'
    config=json.loads(registry.read_text(encoding='utf-8'))
    if config.get('schema')!='unrealbridge.verification_adapters.v1':raise ValueError('Invalid adapter registry')
    adapter=config['adapters'][adapter_id]
    if recipe['content_kind'] not in adapter['content_kinds']:raise ValueError('Adapter kind mismatch')
    module=adapter['module']
    if not re.fullmatch(r'[A-Za-z][A-Za-z0-9_]{0,80}',module):raise ValueError('Invalid registered module')
    path=registry.parent/(module+'.py')
    if path.is_symlink() or path.resolve().parent!=registry.parent.resolve() or hashlib.sha256(path.read_bytes()).hexdigest()!=source_hash:
        raise ValueError('Registered source changed or escaped project')
    worlds=json.loads(unreal.UnrealBridgeWorldLibrary.get_world_contexts(128))
    world=next((w for w in worlds['worlds'] if w['world_handle']==world_handle),None)
    if not world or world['world_type']!='PIE':raise ValueError('Fresh explicit PIE World required')
    world['editor_session_id']=worlds['editor_session_id']
    sandbox=json.loads(unreal.UnrealBridgeSandboxLibrary.get_sandbox_status())
    view={'editor_session_id':worlds['editor_session_id'],'sandbox_id':sandbox.get('root','') if sandbox.get('active') else '',
          'sandbox_generation':sandbox['generation']}
    proof={'original_view':recipe['view_identity'],'current_view':view,'targets':{},'mode':''}
    snapshot=_content_snapshot(recipe['target_packages'])
    if any(not v['exists'] or v['dirty'] for v in snapshot['targets'].values()):raise ValueError('Saved clean target assets required')
    native_path=project.parent/'Saved/UnrealBridge/ProductionOperations'/(recipe['work_order_id']+'.json')
    native=json.loads(native_path.read_text(encoding='utf-8'))
    if native['plan_hash']!=plan_hash or set(native['saved'])!=set(recipe['target_packages']):raise ValueError('Native saved receipts mismatch')
    if sandbox.get('active'):
        lease=sandbox.get('lease_record') or {}
        if lease.get('lease_id')!=recipe['view_identity'].get('lease_id') or lease.get('owner')!=recipe['work_order_id']:
            raise ValueError('Sandbox view mismatch')
        view['lease_id']=lease['lease_id'];proof['mode']='original_saved_sandbox'
    elif recipe['protection']=='file_sandbox':
        name=Path(recipe['view_identity']['sandbox_id']).name
        if not re.fullmatch(r'UB_[A-Za-z0-9_-]+',name,re.I):raise ValueError('Invalid original sandbox')
        lease=json.loads((project.parent/'Saved/UnrealBridge/SandboxLeases'/(name+'.json')).read_text(encoding='utf-8'))
        if lease.get('lease_id')!=recipe['view_identity'].get('lease_id') or lease.get('owner')!=recipe['work_order_id']:
            raise ValueError('Persist receipt owner mismatch')
        outcomes={item['path']:item for item in lease.get('persist_results',[])}
        sealed=lease.get('sealed',{})
        if len(sealed)!=len(recipe['target_packages']) or set(outcomes)!=set(sealed):raise ValueError('Incomplete persist receipt')
        for filename,digest in sealed.items():
            file=Path(filename).resolve()
            if not file.is_relative_to(project.parent.resolve()) or file.suffix.lower()!='.uasset' or file.stat().st_size>32*1024*1024:
                raise ValueError('Persisted target path or budget mismatch')
            actual=hashlib.sha1(file.read_bytes()).hexdigest()
            if not outcomes[filename].get('confirmed') or outcomes[filename]['main_sha1']!=digest or actual!=digest:
                raise ValueError('Main file differs from approved saved sandbox')
            proof['targets'][filename]=actual
        proof['mode']='persisted_main_sha1_verified'
    else:
        if worlds['editor_session_id']!=recipe['view_identity']['editor_session_id']:raise ValueError('Direct-write view requires fresh reviewed contract after restart')
        proof['mode']='original_main_session'
    # Do not reload a running observer and lose its actual accumulated events.
    if str(registry.parent) not in sys.path:sys.path.insert(0,str(registry.parent))
    loaded=importlib.import_module(module)
    if Path(loaded.__file__).resolve()!=path.resolve():raise ValueError('Different module already loaded')
    observed=loaded.verification_snapshot(run_id,recipe,world)
    if observed.get('observer_source_sha256')!=source_hash:raise ValueError('Observer implementation changed since the actual run began')
    if observed.get('editor_session_id')!=worlds['editor_session_id'] or observed.get('world_path')!=world['world_path']:
        raise ValueError('Registered observation identity mismatch')
    observed['values']['saved_variants']=len(native['saved'])
    observed.update(view_identity=view,view_proof=proof,plan_hash=plan_hash,
                    acceptance_hash=_content_hash(recipe['acceptance_profile']),adapter_id=adapter_id,adapter_source_sha256=source_hash)
    return {'ok':True,'schema':'unrealbridge.registered_evidence.v1','observation':observed}
