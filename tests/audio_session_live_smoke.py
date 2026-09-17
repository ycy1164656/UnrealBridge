"""Real audio-device playback, typed routing, isolation, expiry and teardown."""
import argparse
import copy
import json
import time
import uuid
from authoring_live_support import AuthoringLive
from audio_routing_live_smoke import ASSETS,op
from audio_authoring_live_smoke import SOURCES


def main():
    p=argparse.ArgumentParser(); p.add_argument('--project',required=True); p.add_argument('--out',required=True); a=p.parse_args(); h=AuthoringLive(a.project,a.out)
    s=h.server; run=s.network_sessions.NetworkSessionRun(dict(schema=s.network_sessions.SCHEMA,backend='owned_pie',topology='listen',remote_client_count=1,cleanup_policy='always'),lambda operation,values,owner,cleanup:s._runtime_rpc(operation,values,owner,cleanup,project=h.project),h.path.parent/'reports')
    owned=[]; world=None; cleanup_done=False
    def native(function,*args,scoped=True): return h.call('Audio',function,*args,world=world['world_handle'] if scoped else None)
    def source(path,bus=False): return dict(sound_path=path,relative_location_cm=dict(x=100,y=0,z=0),volume_gain=dict(unit='linear',value=.4),pitch_ratio=1,volume_bus_path=ASSETS['bus'] if bus else '')
    def request(sounds,mix=True,bus=False,lease=60):
        return dict(schema='unrealbridge.audio_session.v1',request_id=uuid.uuid4().hex,world_handle=world['world_handle'],audio_device_id=context['audio_device_id'],lease_seconds=lease,sound_mix_path=ASSETS['mix'] if mix else '',control_bus_mix_path=ASSETS['busmix'] if bus else '',sources=[source(x,bus) for x in sounds])
    def begin(value):
        job=s.bridge_submit_audio_mix_session(value,project=h.project)
        if not job.get('job_id'): return job
        result=s.bridge_wait_job(job['job_id'],wait_timeout=15,project=h.project); v=s._last_json_output(result)
        if not v: raise RuntimeError(result)
        if v.get('session_id'): owned.append(v['session_id'])
        return v
    def get(id,scoped=True): return native('get_audio_mix_session',id,world['world_handle'],scoped=scoped)
    def wait(id,condition,seconds=12,scoped=True):
        deadline=time.monotonic()+seconds
        while True:
            v=get(id,scoped); h.report.setdefault('samples',{})[id]=v; h.persist()
            if condition(v): return v
            if time.monotonic()>deadline: raise AssertionError(('Audio condition timed out',v))
            time.sleep(.15)
    def end(id):
        native('end_audio_mix_session',id,world['world_handle']); return wait(id,lambda v:v.get('status')=='ended')
    try:
        h.check('Clean Editor before real audio test',h.editor()=={'pie':False,'dirty':[]}); run.execute(); run.deadline=time.monotonic()+300
        world=next(w for w in run.report['ready']['worlds'] if w['net_mode']=='ListenServer'); context=native('get_audio_mix_context',world['world_handle']); h.report['context']=context
        h.check('Exact live World audio device discovered',context.get('ok') and context.get('audio_modulation_loaded'),context)
        value=request([ASSETS['cue']],True,True); first=begin(value); h.check('Owned native Mix and bus session begins',first.get('ok'),first); firstid=first['session_id']
        repeated=begin(value); h.check('Idempotent begin retains one native session',repeated.get('session_id')==firstid,repeated)
        changed=copy.deepcopy(value); changed['lease_seconds']=61; bad=begin(changed); h.check('Changed payload cannot reuse request ID',not bad.get('ok'),bad)
        bad=request([ASSETS['cue']]); bad['audio_device_id']+=999; h.check('Different audio device rejected',not begin(bad).get('ok'))
        bad=request([ASSETS['source']]); h.check('Wrong source asset class rejected',not begin(bad).get('ok'))
        # Exercise the explicitly owned preview window. Multi-PIE can mute
        # every device except the focused viewport; never change global audio.
        h.report['before_preview_focus']=get(firstid)
        window=h.call('SlateInput','get_owned_pie_window_geometry',run.run_id,world['world_handle'],world=world['world_handle'])
        h.check('Audio preview belongs to the owned PIE window',window.get('ok'),window)
        focus=dict(schema='unrealbridge.pie_window.v1',request_id=uuid.uuid4().hex,run_id=run.run_id,world_handle=world['world_handle'],window_id=window['window_id'],width_px=1280,height_px=720,dpi_scale=max(1,min(2,window['dpi_scale'])))
        focused=h.call('SlateInput','set_owned_pie_window_geometry',json.dumps(focus),world=world['world_handle'])
        h.check('Owned preview window can be brought forward',focused.get('ok'),focused)
        h.report['focused_context']=native('get_audio_mix_context',world['world_handle'])
        real=wait(firstid,lambda v:v['sound_mix_active_refs']==1 and v['bus_mix_active'] and all(x['envelope_samples']>0 and x['peak_envelope']>0 and x['playback_percent_samples']>0 for x in v['sources']))
        h.check('SoundCue produces actual nonzero envelope and wave playback',bool(real['sources'][0]['observed_wave_paths']),real)
        h.check('Real volume ControlBus value responds to active mix',0<real['sources'][0]['volume_bus_value_normalized']<1,real)
        second=begin(request([SOURCES[0],SOURCES[2]],False)); secondid=second['session_id']
        pair=wait(secondid,lambda v:all(x['envelope_samples']>0 and x['peak_envelope']>0 for x in v['sources']))
        h.check('Gun and skill sources both produce nonzero samples',len(pair['sources'])==2,pair); end(secondid)
        # Concurrent authoring changes the source, not the session's unique clone.
        change=h.author(h.request(ASSETS['mix'],'audio.routing',[op('sound_mix_timing','mix',fade_in_seconds=.12,fade_out_seconds=.1,duration_seconds=-1)],False,True))
        h.check('Concurrent source edit saves only the declared Mix',change.get('ok') and change.get('saved'),change)
        h.check('Session reports concurrent source changes',get(firstid)['source_asset_changed'])
        third=begin(request(['/Game/Audio/Sounds/Music/mx_menu_short-pad_01'],True)); thirdid=third['session_id']
        music=wait(thirdid,lambda v:all(x['envelope_samples']>0 and x['peak_envelope']>0 for x in v['sources']))
        h.check('Real music source plays through an independent session',music['sound_mix_active_refs']==1,music)
        stopped=end(firstid); h.check('Ended session removes owned components Mix and bus',not stopped['owned_actor_alive'] and not stopped['sound_mix_present'] and not stopped['bus_mix_active'],stopped)
        h.check('Ending one session preserves the concurrent Mix',get(thirdid)['sound_mix_active_refs']==1)
        h.check('Duplicate End remains ended',end(firstid)['status']=='ended')
        model=native('get_audio_routing_model',json.dumps([ASSETS['mix']]))
        h.check('End does not restore an obsolete source snapshot',abs(model['assets'][ASSETS['mix']]['fade_in_seconds']-.12)<1e-6,model); end(thirdid)
        expiring=begin(request([ASSETS['cue']],True,True,5)); expired=wait(expiring['session_id'],lambda v:v['status']=='ended',12)
        h.check('Lease expiry performs owned audio cleanup',expired['end_reason']=='lease_expired' and not expired['sound_mix_present'],expired)
        last=begin(request([ASSETS['cue']],True,True)); lastid=last['session_id']; wait(lastid,lambda v:v['sound_mix_active_refs']==1 and v['bus_mix_active'])
        h.report['runtime_cleanup']=run.cleanup(); cleanup_done=True
        cleaned=wait(lastid,lambda v:v['status']=='ended',12,False)
        h.check('World teardown cleans the original device and sessions',cleaned['end_reason']=='world_cleanup' and not cleaned['sound_mix_present'] and not cleaned['bus_mix_active'],cleaned)
        h.report['not_verified']=['Human listening acceptance','Music plus dialogue: no dialogue/voice-named SoundWave asset was found; no surrogate labelled dialogue']
        h.finish()
    except Exception as exc:
        h.report.update(status='failed',error=str(exc)); h.persist(); raise
    finally:
        if world:
            for id in set(owned):
                try: native('end_audio_mix_session',id,world['world_handle'],scoped=False)
                except Exception as exc: h.report.setdefault('cleanup_errors',[]).append(str(exc))
        if not cleanup_done:
            h.report['runtime_cleanup']=run.cleanup()
        h.persist()


if __name__=='__main__': main()
