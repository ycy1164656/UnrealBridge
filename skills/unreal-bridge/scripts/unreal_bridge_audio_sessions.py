"""Bounded native audio session v1; no global mix/profile or implicit replay."""
import copy
import json
import unreal_bridge_upgrade as upgrade

SCHEMA='unrealbridge.audio_session.v1'


def normalize(request):
    if not isinstance(request,dict) or set(request)!={'schema','request_id','world_handle','audio_device_id','lease_seconds','sound_mix_path','control_bus_mix_path','sources'} or request.get('schema')!=SCHEMA:
        raise upgrade.UpgradeFault('ValidationFailed','Exact audio session v1 required')
    upgrade.canonical_bytes(request)  # rejects non-finite values before entering native code
    value=copy.deepcopy(request)
    if not isinstance(value['request_id'],str) or not 1<=len(value['request_id'])<=96 or not isinstance(value['world_handle'],str) or not 1<=len(value['world_handle'])<=1024:
        raise upgrade.UpgradeFault('ValidationFailed','Bounded request and exact World identities required')
    if type(value['audio_device_id']) is not int or not 0<=value['audio_device_id']<=0xffffffff or type(value['lease_seconds']) not in (int,float) or not 5<=value['lease_seconds']<=300:
        raise upgrade.UpgradeFault('ValidationFailed','Explicit audio device and 5..300 second lease required')
    if not isinstance(value['sources'],list) or not 1<=len(value['sources'])<=3:
        raise upgrade.UpgradeFault('ValidationFailed','One to three sources required')
    for source in value['sources']:
        if not isinstance(source,dict) or set(source)!={'sound_path','relative_location_cm','volume_gain','pitch_ratio','volume_bus_path'}:
            raise upgrade.UpgradeFault('ValidationFailed','Exact typed audio source required')
        pos=source['relative_location_cm']; gain=source['volume_gain']
        if not isinstance(pos,dict) or set(pos)!={'x','y','z'} or any(type(v) not in (int,float) or abs(v)>5000 for v in pos.values()):
            raise upgrade.UpgradeFault('ValidationFailed','Relative position is in bounded centimetres')
        if not isinstance(gain,dict) or set(gain)!={'unit','value'} or gain['unit'] not in ('linear','db') or type(gain['value']) not in (int,float):
            raise upgrade.UpgradeFault('ValidationFailed','Explicit linear/dB gain required')
        low,high=(0,4) if gain['unit']=='linear' else (-96,12.0411998)
        if not low<=gain['value']<=high or type(source['pitch_ratio']) not in (int,float) or not .125<=source['pitch_ratio']<=8:
            raise upgrade.UpgradeFault('ValidationFailed','Gain or pitch outside verified range')
        for field in ('sound_path','volume_bus_path'):
            if not isinstance(source[field],str) or len(source[field])>512: raise upgrade.UpgradeFault('ValidationFailed','Bounded asset path required')
    for field in ('sound_mix_path','control_bus_mix_path'):
        if not isinstance(value[field],str) or len(value[field])>512: raise upgrade.UpgradeFault('ValidationFailed','Bounded mix path required')
    if len(upgrade.canonical_bytes(value))>65536: raise upgrade.UpgradeFault('ValidationFailed','Audio request exceeds 64 KiB')
    return value


def submit(server,request,**route):
    value=normalize(request); payload=json.dumps(value,sort_keys=True,separators=(',',':'),ensure_ascii=False)
    code='import unreal\nprint(unreal.UnrealBridgeAudioLibrary.begin_audio_mix_session('+repr(payload)+'))'
    return server.bridge_submit_job(code,idempotency_key='audio:'+value['world_handle']+':'+value['request_id'],world_handle=value['world_handle'],run_timeout=30,**route)
