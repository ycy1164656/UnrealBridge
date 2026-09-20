"""Small typed Editor slices for frozen content recipes; no arbitrary code or global save."""
import hashlib
import json
import os
from pathlib import Path
import re

import unreal


def _content_hash(value):
    return hashlib.sha256(json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':'), allow_nan=False).encode('utf-8')).hexdigest()


def _content_snapshot(packages):
    result = json.loads(unreal.UnrealBridgeUpgradeLibrary.get_authoring_snapshot(json.dumps(packages)))
    if not result.get('ok'):
        raise ValueError(result.get('error', 'Snapshot failed'))
    return result


def _content_write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix('.pending')
    with temporary.open('w', encoding='utf-8') as stream:
        json.dump(data, stream, ensure_ascii=False, sort_keys=True, allow_nan=False)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def _content_niagara_float_variables(target, step):
    """Read-only guard shared by execution and adversarial live checks."""
    structure = unreal.UnrealBridgeNiagaraLibrary.get_niagara_system_structure(target)
    actual = {str(e.name): str(e.handle_id).lower() for e in structure.emitters}
    if len(actual) != len(structure.emitters) or actual != {k: v.lower() for k, v in step['expected_emitters'].items()}:
        raise ValueError('Missing, renamed, duplicate or replaced emitter identity')
    params = {str(v.name): v for v in unreal.UnrealBridgeNiagaraLibrary.get_niagara_user_parameters(target)}
    variables = []
    for name, value in step['values'].items():
        param = params.get(name)
        if not param or str(param.type_name) != 'NiagaraFloat' or param.is_data_interface or param.is_u_object:
            raise ValueError('Only existing float defaults; object/dynamic/data-interface replacement is forbidden')
        variables.append({'name': name, 'type': {'classStructOrEnum': {'refPath': '/Script/Niagara.NiagaraFloat'}},
            'defaultValue': {'struct': {'refPath': '/Script/Niagara.NiagaraFloat'}, 'value': {'value': float(value)}}, 'description': ''})
    return variables


def execute_step(recipe, step_id, plan_hash):
    """The host freezes the complete contract; the Editor rechecks scope and every revision."""
    if _content_hash(recipe) != plan_hash or recipe.get('schema') != 'unrealbridge.content_recipe.v1':
        raise ValueError('Frozen plan hash/schema mismatch')
    order = recipe.get('work_order_id', '')
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,96}', order):
        raise ValueError('Invalid work order identity')
    targets = recipe.get('target_packages', [])
    if not isinstance(targets, list) or not 1 <= len(targets) <= 16 or len(set(targets)) != len(targets):
        raise ValueError('Exact bounded target packages required')
    for target in targets:
        if not re.fullmatch(r'/[A-Za-z][A-Za-z0-9_]*/[A-Za-z0-9_/]+', target) or target.split('/')[1].lower() in {'engine', 'script', 'temp', 'memory', 'transient'} or target == recipe['template_ref']:
            raise ValueError('Only explicitly owned content targets; template mutation forbidden')
    steps = recipe.get('steps', [])
    if len(steps) > 32:
        raise ValueError('Step limit exceeded')
    selected = [step for step in steps if step.get('id') == step_id]
    if len(selected) != 1:
        raise ValueError('Unknown or ambiguous frozen step')
    step = selected[0]
    allowed = {
        'create_table': {'source', 'target'},
        'duplicate_asset': {'source', 'target'}, 'copy_table_row': {'source', 'source_row', 'target', 'target_row'},
        'import_audio': {'source_file', 'source_sha256', 'target'},
        'set_audio_routing': {'target', 'sound_class', 'submix', 'attenuation', 'concurrency'},
        'set_niagara_floats': {'target', 'values', 'expected_emitters'},
        'set_table_fields': {'target', 'row', 'values'}, 'set_object_properties': {'target', 'values'},
        'compile': {'target'}, 'save': {'target'}, 'readback': {'target'}}
    operation = step.get('operation')
    if operation not in allowed or set(step) != allowed[operation] | {'id', 'operation'} or step['target'] not in targets:
        raise ValueError('Unsupported typed step or scope')
    snapshot = _content_snapshot(targets)
    template_snapshot = _content_snapshot([recipe['template_ref']])
    project = str(Path(unreal.Paths.get_project_file_path()).resolve()).replace('\\', '/').casefold()
    if project != recipe['project_identity'].replace('\\', '/').casefold() or snapshot['editor_session_id'] != recipe['view_identity']['editor_session_id']:
        raise ValueError('Project/Editor identity changed')
    if unreal.UnrealBridgeEditorLibrary.is_in_pie():
        raise ValueError('Content writes require an idle Editor')
    sandbox = json.loads(unreal.UnrealBridgeSandboxLibrary.get_sandbox_status())
    if recipe['protection'] == 'file_sandbox':
        lease = sandbox.get('lease_record', {})
        if not sandbox.get('owned') or sandbox.get('root') != recipe['view_identity']['sandbox_id'] or lease.get('owner') != order or lease.get('lease_id') != recipe['view_identity'].get('lease_id'):
            raise ValueError('Sandbox view/ownership mismatch; no direct-write fallback')
    else:
        raise ValueError('This content executor requires FileSandbox; declared_changeset is plan-only until an adapter owns a complete native ChangeSet')
    template = template_snapshot['targets'][recipe['template_ref']]
    if template['dirty'] or template['revision'] != recipe['template_fingerprint']:
        raise ValueError('Template changed since freeze')
    journal = Path(unreal.Paths.project_saved_dir()).resolve() / 'UnrealBridge/ProductionOperations' / (order + '.json')
    state = json.loads(journal.read_text(encoding='utf-8')) if journal.exists() else {
        'plan_hash': plan_hash, 'editor_session_id': snapshot['editor_session_id'], 'receipts': {},
        'expected_revisions': dict(recipe['expected_revisions']), 'owned_dirty': [], 'saved': []}
    if state['plan_hash'] != plan_hash or state['editor_session_id'] != snapshot['editor_session_id']:
        raise ValueError('Native journal identity changed; reconcile before continuing')
    prior = state['receipts'].get(step_id)
    if prior:
        if prior['payload_hash'] != _content_hash(step):
            raise ValueError('Idempotency payload conflict')
        if prior['phase'] == 'confirmed':
            return {**prior['result'], 'deduplicated': True}
        if prior['phase']!='reconciled_not_applied':
            raise ValueError('Unknown native side effects; read actual state, do not replay')
    for earlier in steps[:steps.index(step)]:
        if state['receipts'].get(earlier['id'], {}).get('phase') != 'confirmed':
            raise ValueError('Previous typed step has not completed')
    for target in targets:
        current = snapshot['targets'][target]
        if current['revision'] != state['expected_revisions'][target]:
            raise ValueError('Target revision conflict: ' + target)
        if current['dirty'] and target not in state['owned_dirty']:
            raise ValueError('Unowned target Dirty conflict')
    receipt = {'payload_hash': _content_hash(step), 'phase': 'intent'}
    state['receipts'][step_id] = receipt
    _content_write(journal, state)
    target = step['target']
    try:
        if operation == 'create_table':
            if step['source'] != recipe['template_ref'] or snapshot['targets'][target]['exists']:
                raise ValueError('Create requires the frozen struct template and an absent target')
            struct = unreal.UnrealBridgeDataTableLibrary.get_data_table_row_struct_path(step['source'])
            if not struct:
                raise ValueError('Template row struct unavailable')
            directory, name = target.rsplit('/', 1)
            created = unreal.UnrealBridgeAssetFactoryLibrary.create_data_table(directory, name, struct)
            if not created:
                raise RuntimeError('Typed table creation failed')
            # This existing factory saves its exact new asset, into the active sandbox view.
            state['saved'] = sorted(set(state['saved'] + [target]))
        elif operation == 'duplicate_asset':
            if step['source'] != recipe['template_ref'] and step['source'] not in targets:
                raise ValueError('Source outside the frozen dependency set')
            if snapshot['targets'][target]['exists']:
                raise ValueError('Existing asset cannot be overwritten by duplication')
            created = unreal.EditorAssetLibrary.duplicate_asset(step['source'], target)
            if not created:
                raise RuntimeError('Editor duplication failed')
        elif operation == 'import_audio':
            import wave
            source = Path(step['source_file'])
            for part in [source,*source.parents]:
                if part.is_symlink() or getattr(part,'is_junction',lambda:False)():
                    raise ValueError('Audio source link/junction rejected')
            source=source.resolve(strict=True)
            roots=[Path(p).resolve(strict=True) for p in recipe['bindings'].get('audio_staging_roots',[])]
            if not any(source.is_relative_to(p) for p in roots) or source.suffix.lower()!='.wav' or source.stat().st_size>16*1024*1024:
                raise ValueError('Audio source outside frozen staging scope/budget')
            if hashlib.sha256(source.read_bytes()).hexdigest()!=step['source_sha256'] or snapshot['targets'][target]['exists']:
                raise ValueError('Audio source changed or import would overwrite target')
            with wave.open(str(source),'rb') as audio:
                if audio.getnchannels() not in (1,2) or audio.getsampwidth()!=2 or not 0<audio.getnframes()/audio.getframerate()<=10:
                    raise ValueError('Short PCM16 SFX required')
                if len(audio.readframes(audio.getnframes()))!=audio.getnframes()*audio.getnchannels()*2:
                    raise ValueError('Truncated WAVE')
            directory,name=target.rsplit('/',1)
            task=unreal.AssetImportTask()
            for key,value in {'filename':str(source),'destination_path':directory,'destination_name':name,
                              'automated':True,'replace_existing':False,'save':False,'factory':unreal.SoundFactory()}.items():
                task.set_editor_property(key,value)
            unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
            imported=unreal.EditorAssetLibrary.load_asset(target)
            if not isinstance(imported,unreal.SoundWave):raise RuntimeError('SoundWave import failed')
        elif operation == 'set_audio_routing':
            sound=unreal.EditorAssetLibrary.load_asset(target)
            if not isinstance(sound,unreal.SoundBase):raise ValueError('SoundBase target required')
            routes={'sound_class':('sound_class_object',unreal.SoundClass),'submix':('sound_submix_object',unreal.SoundSubmixBase),
                    'attenuation':('attenuation_settings',unreal.SoundAttenuation),'concurrency':('concurrency_set',unreal.SoundConcurrency)}
            with unreal.ScopedEditorTransaction('UnrealBridge scoped SFX routing'):
                sound.modify()
                for field,(property_name,expected_class) in routes.items():
                    if not step[field]:continue
                    value=unreal.EditorAssetLibrary.load_asset(step[field])
                    if not isinstance(value,expected_class):raise ValueError('Audio routing asset type mismatch: '+field)
                    sound.set_editor_property(property_name,{value} if field=='concurrency' else value)
        elif operation == 'set_niagara_floats':
            variables=_content_niagara_float_variables(target,step)
            object_path=target+'.'+target.rsplit('/',1)[-1]
            calls=[{'toolset':'NiagaraToolsets.NiagaraToolset_System','tool':'AddUserVariables',
                    'arguments':{'system':{'refPath':object_path},'variablesToAdd':variables}},
                   {'toolset':'NiagaraToolsets.NiagaraToolset_System','tool':'GetUserVariables','arguments':{'system':{'refPath':object_path}}}]
            changed=json.loads(unreal.UnrealBridgeUE58Library.execute_official_transactional_toolset_batch(json.dumps(calls),[target],True,target,'[]'))
            if not changed.get('success'):raise RuntimeError('Typed Niagara transaction failed: '+json.dumps(changed)[:1500])
            state['niagara_readback']=changed
        elif operation == 'copy_table_row':
            if step['source'] != recipe['template_ref'] and step['source'] not in targets:
                raise ValueError('Unresolved row source')
            fields = dict(unreal.UnrealBridgeDataTableLibrary.get_data_table_row_as_map(step['source'], step['source_row']))
            if not fields or unreal.UnrealBridgeDataTableLibrary.does_data_table_row_exist(target, step['target_row']):
                raise ValueError('Missing source row or existing destination row')
            if not unreal.UnrealBridgeDataTableLibrary.add_data_table_row(target, step['target_row'], fields):
                raise RuntimeError('Row registration failed')
        elif operation == 'set_table_fields':
            fields = {str(k): str(v) for k, v in step['values'].items()}
            if not unreal.UnrealBridgeDataTableLibrary.set_data_table_row_fields(target, step['row'], fields):
                raise RuntimeError('Typed row edit failed')
        elif operation == 'set_object_properties':
            obj = unreal.EditorAssetLibrary.load_asset(target)
            if not obj or isinstance(obj, unreal.Blueprint):
                raise ValueError('Blueprint defaults require a dedicated typed authoring operation')
            with unreal.ScopedEditorTransaction('UnrealBridge content properties'):
                obj.modify()
                for key, value in step['values'].items():
                    if not re.fullmatch(r'[A-Za-z][A-Za-z0-9_]{0,95}', key):
                        raise ValueError('Flat property name required')
                    obj.set_editor_property(key, value)
        elif operation == 'compile':
            obj = unreal.EditorAssetLibrary.load_asset(target)
            if isinstance(obj, unreal.Blueprint):
                unreal.BlueprintEditorLibrary.compile_blueprint(obj)
                if str(obj.get_editor_property('status')).endswith('ERROR'):
                    raise RuntimeError('Blueprint compile failed')
            if unreal.UnrealBridgeEditorLibrary.get_asset_compile_job_count():
                raise RuntimeError('Compilation pending; reconcile and poll before saving')
        elif operation == 'save':
            obj = unreal.EditorAssetLibrary.load_asset(target)
            if not obj or not unreal.EditorAssetLibrary.save_loaded_asset(obj, only_if_is_dirty=False):
                raise RuntimeError('Exact target save failed')
            state['saved'] = sorted(set(state['saved'] + [target]))
        elif operation == 'readback':
            if target not in state['saved']:
                raise ValueError('Readback requires a confirmed target save')
        after = _content_snapshot(targets)
        state['expected_revisions'] = {p: after['targets'][p]['revision'] for p in targets}
        state['owned_dirty'] = [p for p in targets if after['targets'][p]['dirty']]
        result = {'ok': True, 'step_id': step_id, 'operation': operation, 'target': target,
                  'revision': after['targets'][target]['revision'], 'dirty': after['targets'][target]['dirty'],
                  'saved_packages': state['saved'], 'view': 'sandbox' if sandbox.get('active') else 'main_project',
                  'readback': after['targets'][target], 'human_accepted': False}
        if operation=='set_niagara_floats':
            result['niagara_readback']=state['niagara_readback']
        if operation == 'readback' and isinstance(unreal.EditorAssetLibrary.load_asset(target), unreal.DataTable):
            names = list(unreal.UnrealBridgeDataTableLibrary.get_data_table_row_names(target))
            result['rows'] = names
            result['changed_fields'] = {s['row']: dict(unreal.UnrealBridgeDataTableLibrary.get_data_table_row_as_map(target, s['row']))
                                       for s in steps if s['operation'] == 'set_table_fields' and s['target'] == target}
        receipt.update(phase='confirmed', result=result)
        _content_write(journal, state)
        return result
    except Exception as error:
        receipt.update(phase='outcome_unknown', error=str(error)[:1024])
        _content_write(journal, state)
        raise


def reconcile_unchanged_step(recipe,step_id,plan_hash):
    """Permit a new attempt only after exact unchanged in-memory preimages are proved.

    Limited to property edits with no external I/O. Never rewinds an asset, save,
    import, duplication or uncertain external operation. Prior failures are retained.
    """
    if _content_hash(recipe)!=plan_hash:raise ValueError('Frozen plan changed')
    step=next(s for s in recipe['steps'] if s['id']==step_id)
    if step['operation'] not in {'set_niagara_floats','set_table_fields','set_object_properties','set_audio_routing'}:
        raise ValueError('This operation cannot be reconciled by an unchanged asset preimage')
    order=recipe['work_order_id']
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,96}',order):raise ValueError('Invalid order')
    path=Path(unreal.Paths.project_saved_dir()).resolve()/'UnrealBridge/ProductionOperations'/(order+'.json')
    state=json.loads(path.read_text(encoding='utf-8'));prior=state['receipts'][step_id]
    if prior['phase']!='outcome_unknown' or state['plan_hash']!=plan_hash:raise ValueError('No uncertain matching step to reconcile')
    snapshot=_content_snapshot(recipe['target_packages'])
    if snapshot['editor_session_id']!=state['editor_session_id'] or unreal.UnrealBridgeEditorLibrary.is_in_pie():
        raise ValueError('Editor/session changed')
    sandbox=json.loads(unreal.UnrealBridgeSandboxLibrary.get_sandbox_status())
    if recipe['protection']=='file_sandbox' and ((sandbox.get('lease_record') or {}).get('lease_id')!=recipe['view_identity'].get('lease_id') or not sandbox.get('owned')):
        raise ValueError('Sandbox lease changed')
    for target,item in snapshot['targets'].items():
        if item['revision']!=state['expected_revisions'][target] or item['dirty']!=(target in state['owned_dirty']):
            raise ValueError('Target preimage changed; manual reconciliation required')
    dirty={x.get_path_name() for x in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()+unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()}
    if dirty-set(state['owned_dirty']):raise ValueError('Unexpected Dirty packages; preserve current state')
    evidence={'ok':True,'step_id':step_id,'phase':'reconciled_not_applied','preimages':snapshot['targets'],'prior':dict(prior)}
    state.setdefault('reconciliations',[]).append(evidence)
    if len(state['reconciliations'])>2:raise ValueError('Reconciliation retry budget exhausted')
    prior['phase']='reconciled_not_applied';_content_write(path,state)
    return evidence


def accept_reviewed_revision(recipe, plan_hash, target, prior_revision, reviewed_revision, review_reason):
    """Explicit reviewed precondition amendment, never automatic repair or acceptance.

    An asynchronous compiler can replace derived subobjects after a confirmed
    edit. Preserve the old checkpoint and require an exact fresh review; do not
    weaken the frozen plan, write set, parameter values or acceptance profile.
    """
    if _content_hash(recipe)!=plan_hash or target not in recipe['target_packages']:
        raise ValueError('Frozen scope mismatch')
    if not isinstance(review_reason,str) or not 30<=len(review_reason)<=2048:
        raise ValueError('Concrete independent revision review required')
    snapshot=_content_snapshot(recipe['target_packages'])
    sandbox=json.loads(unreal.UnrealBridgeSandboxLibrary.get_sandbox_status())
    if unreal.UnrealBridgeEditorLibrary.is_in_pie() or snapshot['editor_session_id']!=recipe['view_identity']['editor_session_id']:
        raise ValueError('Idle original Editor required')
    if recipe['protection']=='file_sandbox' and ((sandbox.get('lease_record') or {}).get('lease_id')!=recipe['view_identity'].get('lease_id') or not sandbox.get('owned')):
        raise ValueError('Exact sandbox lease required')
    path=Path(unreal.Paths.project_saved_dir()).resolve()/'UnrealBridge/ProductionOperations'/(recipe['work_order_id']+'.json')
    state=json.loads(path.read_text(encoding='utf-8'))
    if state['plan_hash']!=plan_hash or state['expected_revisions'][target]!=prior_revision:
        raise ValueError('Prior checkpoint changed')
    if snapshot['targets'][target]['revision']!=reviewed_revision:
        raise ValueError('Reviewed revision changed again')
    for package,item in snapshot['targets'].items():
        if package!=target and item['revision']!=state['expected_revisions'][package]:
            raise ValueError('Other target changed')
        if item['dirty']!=(package in state['owned_dirty']):raise ValueError('Dirty ownership changed')
    if _content_snapshot([recipe['template_ref']])['targets'][recipe['template_ref']]['revision']!=recipe['template_fingerprint']:
        raise ValueError('Template changed')
    history=state.setdefault('reviewed_revision_amendments',[])
    if len(history)>=2:raise ValueError('Independent review budget exhausted')
    evidence={'target':target,'prior_revision':prior_revision,'reviewed_revision':reviewed_revision,
              'reason':review_reason,'plan_hash':plan_hash,'automatic':False,'acceptance_changed':False}
    history.append(evidence);state['expected_revisions'][target]=reviewed_revision
    _content_write(path,state)
    return {'ok':True,'review':evidence}
