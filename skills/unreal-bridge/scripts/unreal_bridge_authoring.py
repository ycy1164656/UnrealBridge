"""Explicit incremental authoring envelope; reuses upgrade v1 context unchanged."""
import copy
import json
import unreal_bridge_upgrade as upgrade

SCHEMA='unrealbridge.authoring.v1'
EXECUTORS={
    'anim.montage_segments':('Anim','preview_montage_segment_ops','apply_montage_segment_ops'),
    'anim.notify_add':('Anim','add_typed_anim_notify','add_typed_anim_notify'),
    'anim.notify_update':('Anim','update_typed_anim_notify','update_typed_anim_notify'),
    'ai.behavior_tree':('AI','preview_behavior_tree_ops','apply_behavior_tree_ops'),
    'ai.blackboard_keys':('AI','apply_blackboard_key_ops','apply_blackboard_key_ops'),
    'audio.sound_cue':('Audio','preview_sound_cue_ops','apply_sound_cue_ops'),
    'audio.routing':('Audio','preview_audio_routing_ops','apply_audio_routing_ops'),
}


def normalize(request):
    if not isinstance(request,dict) or set(request)!={'schema','operation_id','context','operations','save_policy'} or request.get('schema')!=SCHEMA:
        raise upgrade.UpgradeFault('ValidationFailed','Exact incremental authoring envelope required')
    if request.get('operation_id') not in EXECUTORS: raise upgrade.UpgradeFault('UnsupportedCapability','No implemented authoring executor')
    if len(upgrade.canonical_bytes(request))>65536: raise upgrade.UpgradeFault('ValidationFailed','Authoring request exceeds 64 KiB')
    value=copy.deepcopy(request); value['context']=upgrade.normalize_request(value['context'])
    if value['operation_id']!='audio.routing' and len(value['context']['target_packages'])!=1: raise upgrade.UpgradeFault('ScopeViolation','Exactly one asset target per incremental request except bounded audio routing')
    if value['save_policy'] not in ('never','declared_targets') or (value['context']['dry_run'] and value['save_policy']!='never'):
        raise upgrade.UpgradeFault('ScopeViolation','Dry run never saves; write save policy is explicit')
    operations=value['operations']
    if not isinstance(operations,list) or not 1<=len(operations)<=64 or any(not isinstance(op,dict) for op in operations):
        raise upgrade.UpgradeFault('ValidationFailed','One to 64 typed operations required')
    operation=value['operation_id']
    if operation=='anim.montage_segments':
        common={'sequence_path','source_start_seconds','source_end_seconds','montage_start_seconds','play_rate','loop_count'}
        fields={'create_montage':{'op','skeleton_path'},'add_slot':{'op','slot_name'},'add_segment':{'op','slot_name'}|common,
                'update_segment':{'op','segment'}|common,'move_segment':{'op','segment','slot_name','montage_start_seconds'}}
        for op in operations:
            if op.get('op') not in fields or set(op)!=fields[op['op']]: raise upgrade.UpgradeFault('ValidationFailed','Unsupported segment operation or fields')
            for field in common-{'sequence_path','loop_count'}:
                if field in op and (type(op[field]) not in (int,float) or op[field]<0): raise upgrade.UpgradeFault('ValidationFailed','Finite nonnegative segment values required')
            if 'play_rate' in op and not .01<=op['play_rate']<=100: raise upgrade.UpgradeFault('ValidationFailed','Positive verified play rates only')
            if 'loop_count' in op and (type(op['loop_count']) is not int or not 1<=op['loop_count']<=100): raise upgrade.UpgradeFault('ValidationFailed','Loop count must be 1..100')
    elif operation.startswith('anim.notify_'):
        fields={'notify_guid','class_path','kind','track_index','track_name','time_seconds','duration_seconds','properties'}
        for op in operations:
            if set(op)!=fields or op['kind'] not in ('notify','state') or not isinstance(op['properties'],dict) or len(op['properties'])>16:
                raise upgrade.UpgradeFault('ValidationFailed','Exact typed notify contract required')
            for name,prop in op['properties'].items():
                if not isinstance(name,str) or '.' in name or '[' in name or not isinstance(prop,dict) or set(prop)!={'type','value'} or prop['type'] not in ('bool','float','name','asset'):
                    raise upgrade.UpgradeFault('ValidationFailed','Property paths and arbitrary expressions are forbidden')
    elif operation=='ai.behavior_tree':
        fields={'create_tree':{'op','blackboard_path','root_guid'},'add_node':{'op','node_guid','kind','class_path','x','y','properties'},
            'attach_decorator':{'op','node_guid','parent_guid','class_path','properties'},'attach_service':{'op','node_guid','parent_guid','class_path','properties'},
            'set_property':{'op','node_guid','properties'},'connect':{'op','parent_guid','child_guid'},'reparent':{'op','parent_guid','child_guid'},
            'reorder_children':{'op','parent_guid','children'},'set_location':{'op','node_guid','x','y'}}
        for op in operations:
            if op.get('op') not in fields or set(op)!=fields[op['op']]: raise upgrade.UpgradeFault('ValidationFailed','Unsupported BT operation or fields')
            if 'properties' in op and (not isinstance(op['properties'],dict) or len(op['properties'])>16): raise upgrade.UpgradeFault('ValidationFailed','Bounded typed BT properties required')
            if 'children' in op and (not isinstance(op['children'],list) or len(op['children'])>128 or len(set(op['children']))!=len(op['children'])): raise upgrade.UpgradeFault('ValidationFailed','Unique ordered child GUIDs required')
            for axis in ('x','y'):
                if axis in op and (type(op[axis]) is not int or abs(op[axis])>100000): raise upgrade.UpgradeFault('ValidationFailed','Bounded integer graph location required')
    elif operation=='ai.blackboard_keys':
        for op in operations:
            if set(op)!={'op','key_name','key_type'} or op['op']!='add_key' or op['key_type'] not in ('bool','int','float','vector','object'): raise upgrade.UpgradeFault('ValidationFailed','Verified Blackboard key additions only')
    elif operation=='audio.sound_cue':
        fields={'create_cue':{'op'},'add_node':{'op','node_guid','kind','input_count','x','y','properties'},
            'set_property':{'op','node_guid','properties'},'set_location':{'op','node_guid','x','y'},
            'connect':{'op','source_guid','destination_guid','input_index'},'set_output':{'op','source_guid'}}
        for op in operations:
            if op.get('op') not in fields or set(op)!=fields[op['op']]: raise upgrade.UpgradeFault('ValidationFailed','Unsupported SoundCue operation or fields')
            if 'kind' in op and op['kind'] not in ('wave','mixer','random','modulator','attenuation'): raise upgrade.UpgradeFault('UnsupportedCapability','SoundNode kind is not implemented')
            if 'properties' in op and (not isinstance(op['properties'],dict) or len(op['properties'])>8): raise upgrade.UpgradeFault('ValidationFailed','Bounded typed sound properties required')
            for key in ('input_count','input_index','x','y'):
                if key in op and (type(op[key]) is not int or abs(op[key])>100000): raise upgrade.UpgradeFault('ValidationFailed','Finite integer pin/layout fields required')
    elif operation=='audio.routing':
        fields={'create_asset':{'kind'},'sound_class_defaults':{'gain','pitch_ratio'},'sound_class_parent':{'parent'},'submix_parent':{'parent'},
            'sound_mix_override':{'sound_class','gain','pitch_ratio','apply_to_children'},'sound_mix_timing':{'fade_in_seconds','fade_out_seconds','duration_seconds'},
            'cue_class':{'sound_class'},'cue_base_submix':{'submix'},'cue_submix_send':{'submix','gain'},
            'control_bus_parameter':{'parameter','bypass'},'control_bus_mix_stage':{'bus','value_normalized','attack_seconds','release_seconds'}}
        for op in operations:
            if op.get('op') not in fields or set(op)!=fields[op['op']]|{'op','target'}: raise upgrade.UpgradeFault('ValidationFailed','Exact typed audio routing operation required')
            if op['target'] not in value['context']['target_packages']: raise upgrade.UpgradeFault('ScopeViolation','Audio operation target is not declared')
            if 'gain' in op:
                gain=op['gain']
                if not isinstance(gain,dict) or set(gain)!={'unit','value'} or gain['unit'] not in ('linear','db') or type(gain['value']) not in (int,float):
                    raise upgrade.UpgradeFault('ValidationFailed','Explicit linear or dB gain required')
                lo,hi=(0,4) if gain['unit']=='linear' else (-96,12.0411998)
                if not lo<=gain['value']<=hi: raise upgrade.UpgradeFault('ValidationFailed','Gain outside verified limits')
            for name in ('pitch_ratio','fade_in_seconds','fade_out_seconds','duration_seconds','value_normalized','attack_seconds','release_seconds'):
                if name in op and type(op[name]) not in (int,float): raise upgrade.UpgradeFault('ValidationFailed','Typed numeric units required')
    return value


def submit(server,request,**route):
    value=normalize(request); context=value['context']; library,preview,apply=EXECUTORS[value['operation_id']]
    function=preview if context['dry_run'] else apply
    payload=json.dumps(value,sort_keys=True,separators=(',',':'),ensure_ascii=False)
    code='import unreal\nprint(unreal.UnrealBridge'+library+'Library.'+function+'('+repr(payload)+'))'
    return server.bridge_submit_job(code,idempotency_key='authoring:'+context['editor_session_id']+':'+context['request_id'],
        world_handle=context.get('world_handle') or None,run_timeout=context['timeout_seconds'],**route)
