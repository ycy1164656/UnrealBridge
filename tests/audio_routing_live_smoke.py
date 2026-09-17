"""Exact eight-package audio routing transaction and negative preflight checks."""
import argparse
from authoring_live_support import AuthoringLive

ROOT='/ShooterRoyal/Automation/BridgeUpgrade/Audio/'
ASSETS={k:ROOT+n for k,n in dict(master='SCL_SRUB_Master',source='SCL_SRUB_Source',mix='SMIX_SRUB_Test',submix_master='SMX_SRUB_Master',submix_source='SMX_SRUB_Source',bus='CB_SRUB_Volume',busmix='CBM_SRUB_Test',cue='SC_SRUB_Authoring').items()}
KINDS=dict(master='sound_class',source='sound_class',mix='sound_mix',submix_master='submix',submix_source='submix',bus='control_bus',busmix='control_bus_mix')


def op(action,target,**kw): return dict(op=action,target=ASSETS[target],**kw)


def routing_ops():
    return [op('sound_class_defaults','master',gain=dict(unit='linear',value=1),pitch_ratio=1),op('sound_class_defaults','source',gain=dict(unit='db',value=-3),pitch_ratio=1),
        op('sound_class_parent','source',parent=ASSETS['master']),op('submix_parent','submix_source',parent=ASSETS['submix_master']),
        op('sound_mix_override','mix',sound_class=ASSETS['source'],gain=dict(unit='db',value=-6),pitch_ratio=1,apply_to_children=False),
        op('sound_mix_timing','mix',fade_in_seconds=.1,fade_out_seconds=.1,duration_seconds=-1),
        op('cue_class','cue',sound_class=ASSETS['source']),op('cue_base_submix','cue',submix=ASSETS['submix_master']),
        op('cue_submix_send','cue',submix=ASSETS['submix_source'],gain=dict(unit='linear',value=.1)),
        op('control_bus_parameter','bus',parameter='/AudioModulation/Volume',bypass=False),
        op('control_bus_mix_stage','busmix',bus=ASSETS['bus'],value_normalized=.9,attack_seconds=.1,release_seconds=.1)]


def main():
    p=argparse.ArgumentParser(); p.add_argument('--project',required=True); p.add_argument('--out',required=True); p.add_argument('--reopen-check',action='store_true'); a=p.parse_args()
    h=AuthoringLive(a.project,a.out); targets=list(ASSETS.values())
    try:
        h.check('Clean unused Editor baseline',h.editor()=={'pie':False,'dirty':[]})
        if not a.reopen_check:
            before=h.snapshot(targets); missing=[k for k in KINDS if not before['targets'][ASSETS[k]]['exists']]
            ops=[op('create_asset',k,kind=KINDS[k]) for k in missing]+routing_ops()
            value=h.author(h.request(targets,'audio.routing',ops)); h.check('Eight-target routing preview validates',value.get('ok'),value)
            h.check('Routing preview leaves all packages unchanged',h.snapshot(targets)['targets']==before['targets'] and h.editor()=={'pie':False,'dirty':[]})
            request=h.request(targets,'audio.routing',ops,False,True); value=h.author(request)
            h.check('Only declared routing targets saved',value.get('ok') and value.get('saved'),value)
            h.check('Duplicate routing apply does not add stages or parents',h.author(request)==value)
            edit=[op('control_bus_mix_stage','busmix',bus=ASSETS['bus'],value_normalized=.9,attack_seconds=.15,release_seconds=.1)]
            preview=h.author(h.request(targets,'audio.routing',edit)); applied=h.author(h.request(targets,'audio.routing',edit,False,True))
            h.check('Existing bus reference remaps consistently in preview/apply',preview.get('ok') and applied.get('ok') and preview['model']['assets'][ASSETS['busmix']]==applied['model']['assets'][ASSETS['busmix']],dict(preview=preview,applied=applied))
            revision=h.snapshot(targets)['targets']
            negatives=[('SoundClass parent cycle',op('sound_class_parent','master',parent=ASSETS['source'])),('Submix cycle',op('submix_parent','submix_master',parent=ASSETS['submix_source'])),
                ('Wrong reference class',op('cue_class','cue',sound_class=ASSETS['mix'])),('Negative send gain',op('cue_submix_send','cue',submix=ASSETS['submix_source'],gain=dict(unit='linear',value=-1))),
                ('Missing gain unit',op('sound_class_defaults','source',gain=dict(value=.5),pitch_ratio=1)),('Normalized stage range',op('control_bus_mix_stage','busmix',bus=ASSETS['bus'],value_normalized=2,attack_seconds=.1,release_seconds=.1)),
                ('Wrong parameter class',op('control_bus_parameter','bus',parameter=ASSETS['mix'],bypass=False))]
            for name,bad in negatives:
                value=h.author(h.request(targets,'audio.routing',[bad])); h.check(name+' rejected without mutations',not value.get('ok') and h.snapshot(targets)['targets']==revision,value)
            value=h.author(h.request(ASSETS['source'],'audio.routing',[op('sound_class_parent','source',parent='')]))
            h.check('Undeclared old parent cannot be mutated',not value.get('ok') and h.snapshot(targets)['targets']==revision,value)
        import json
        value=h.call('Audio','get_audio_routing_model',json.dumps(targets)); h.report['final_model']=value; m=value.get('assets',{})
        h.check('All supported routing systems read back',value.get('ok') and len(m)==8 and value['audio_modulation_loaded'],value)
        h.check('SoundClass parent and dB conversion persist',m[ASSETS['source']]['parent']==ASSETS['master'] and abs(m[ASSETS['source']]['gain_linear']-10**(-3/20))<1e-6)
        h.check('Submix parent and explicit send persist',m[ASSETS['submix_source']]['parent']==ASSETS['submix_master'] and m[ASSETS['cue']]['sends'][0]['submix']==ASSETS['submix_source'])
        h.check('Bus has one typed stage and Volume parameter',m[ASSETS['bus']]['parameter']=='/AudioModulation/Volume' and len(m[ASSETS['busmix']]['stages'])==1 and abs(m[ASSETS['busmix']]['stages'][0]['value_normalized']-.9)<1e-6)
        h.finish()
    except Exception as exc:
        h.report.update(status='failed',error=str(exc)); h.persist(); raise


if __name__=='__main__': main()
