"""SoundCue graph/runtime round trip with existing non-empty ShooterRoyal waves."""
import argparse
from authoring_live_support import AuthoringLive

TARGET='/ShooterRoyal/Automation/BridgeUpgrade/Audio/SC_SRUB_Authoring'
G={name:f'276080000000000000000000000000{i:02d}' for i,name in enumerate(['fire','hit','random','ability','mixer','modulator','attenuation'],1)}
SOURCES=['/ShooterRoyal/Audio/Weapons/Elements/Physical/S_SR_Element_Physical_Fire','/ShooterRoyal/Audio/Weapons/Elements/Shock/S_SR_Element_Shock_Hit','/ShooterRoyal/Audio/Abilities/S_DPS_ClusterGrenade']


def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--project',required=True); parser.add_argument('--out',required=True); parser.add_argument('--reopen-check',action='store_true')
    args=parser.parse_args(); h=AuthoringLive(args.project,args.out)
    try:
        h.check('Clean unused Editor baseline',h.editor()=={'pie':False,'dirty':[]})
        if not args.reopen_check:
            def node(name,kind,count,x,y,props): return dict(op='add_node',node_guid=G[name],kind=kind,input_count=count,x=x,y=y,properties=props)
            def wire(src,dst,index): return dict(op='connect',source_guid=G[src],destination_guid=G[dst],input_index=index)
            props={'pitch_min_ratio':.98,'pitch_max_ratio':1.02,'volume_min_linear':.8,'volume_max_linear':1}
            ops=[dict(op='create_cue'),node('fire','wave',0,-1000,-200,dict(wave_path=SOURCES[0],looping=False)),node('hit','wave',0,-1000,0,dict(wave_path=SOURCES[1],looping=False)),
                node('random','random',2,-700,-100,dict(weights=[1,2],without_replacement=True)),node('ability','wave',0,-700,200,dict(wave_path=SOURCES[2],looping=True)),
                node('mixer','mixer',2,-400,0,dict(input_gains_linear=[.6,.4])),node('modulator','modulator',1,-200,0,props),
                node('attenuation','attenuation',1,0,0,dict(inner_radius_cm=200,falloff_cm=2000,spatialize=True)),
                wire('fire','random',0),wire('hit','random',1),wire('random','mixer',0),wire('ability','mixer',1),wire('mixer','modulator',0),wire('modulator','attenuation',0),dict(op='set_output',source_guid=G['attenuation'])]
            if not h.snapshot(TARGET)['targets'][TARGET]['exists']:
                preview=h.author(h.request(TARGET,'audio.sound_cue',ops)); h.check('Seven native SoundNodes preview with runtime links',preview.get('ok') and len(preview['model']['nodes'])==7,preview)
                h.check('SoundCue dry run creates no asset',not h.snapshot(TARGET)['targets'][TARGET]['exists'])
                request=h.request(TARGET,'audio.sound_cue',ops,False,True); result=h.author(request)
                h.check('Only declared Cue persisted',result.get('ok') and result['saved'],result)
                h.check('Idempotent graph request keeps original nodes',h.author(request)==result)
            edit=[dict(op='set_property',node_guid=G['modulator'],properties=dict(props,volume_min_linear=.7)),dict(op='set_location',node_guid=G['mixer'],x=-420,y=10)]
            result=h.author(h.request(TARGET,'audio.sound_cue',edit,False,True)); h.check('Typed incremental update and layout persist',result.get('ok') and result['saved'],result)
            revision=h.snapshot(TARGET)['targets'][TARGET]['revision']
            negatives=[('Cycle',dict(op='connect',source_guid=G['attenuation'],destination_guid=G['modulator'],input_index=0)),
                ('Zero random weights',dict(op='set_property',node_guid=G['random'],properties=dict(weights=[0,0],without_replacement=True))),
                ('Invalid source class',dict(op='set_property',node_guid=G['fire'],properties=dict(wave_path=TARGET,looping=False))),
                ('Output disconnect',dict(op='set_output',source_guid='0'*32)),
                ('Unreachable existing nodes',dict(op='set_output',source_guid=G['fire'])),
                ('Bad gain range',dict(op='set_property',node_guid=G['mixer'],properties=dict(input_gains_linear=[-1,1]))),
                ('Weight/input mismatch',dict(op='set_property',node_guid=G['random'],properties=dict(weights=[1],without_replacement=True)))]
            for label,bad in negatives:
                value=h.author(h.request(TARGET,'audio.sound_cue',[bad])); h.check(label+' rejected before asset mutation',not value.get('ok') and h.snapshot(TARGET)['targets'][TARGET]['revision']==revision,value)
        model=h.call('Audio','validate_sound_cue_asset',TARGET); h.report['final_model']=model
        h.check('Editor graph and runtime node tree agree',model.get('ok') and len(model['nodes'])==7 and model['output_node_guid']==G['attenuation'],model)
        waves=[x for x in model['nodes'] if x['kind']=='wave']
        h.check('Three actual wave references persisted',len(waves)==3 and all(x['properties']['wave_path'].split('.')[0] in SOURCES for x in waves),waves)
        h.finish()
    except Exception as exc:
        h.report.update(status='failed',error=str(exc)); h.persist(); raise


if __name__=='__main__': main()
