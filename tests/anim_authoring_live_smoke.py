"""Authorized incremental Montage/typed Notify creation, round trip and negatives."""
import argparse
import copy
import json
import uuid
from authoring_live_support import AuthoringLive

BASE='/ShooterRoyal/Automation/BridgeUpgrade/Animation/'
TARGET=BASE+'AM_SRUB_Segments'
SOURCE='/Game/Characters/Heroes/Mannequin/Animations/Actions/MM_Rifle_GrenadeToss.MM_Rifle_GrenadeToss'
ADDITIVE='/Game/Characters/Heroes/Mannequin/Animations/Actions/MM_Rifle_GrenadeToss_Additive.MM_Rifle_GrenadeToss_Additive'
SKELETON='/Game/Characters/Heroes/Mannequin/Meshes/SK_Mannequin.SK_Mannequin'
NATIVE='/Script/ShooterRoyalRuntime.SRAnimNotify_AutomationMarker'
STATE='/Script/ShooterRoyalRuntime.SRAnimNotifyState_AutomationWindow'
GUIDS=['27600000000000000000000000000001','27600000000000000000000000000002','27600000000000000000000000000003']


def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--project',required=True); parser.add_argument('--out',required=True); parser.add_argument('--reopen-check',action='store_true')
    args=parser.parse_args(); h=AuthoringLive(args.project,args.out)
    try:
        h.report['before']=h.editor(); h.check('Clean idle baseline',h.report['before']=={'pie':False,'dirty':[]},h.report['before'])
        if not args.reopen_check:
            # This code only creates the explicitly reported two helper assets if absent.
            helpers=h.execute("import json,unreal\nfrom unreal_bridge import AssetFactory\n"+
                f"base={BASE!r}\nsource={SOURCE!r}\n"+
                "assert not unreal.UnrealBridgeEditorLibrary.get_dirty_package_names()\n"+
                "paths=[base+'AS_SRUB_SourceRate',base+'AN_SRUB_Marker']\n"+
                "created=[]\n"+
                "if not unreal.EditorAssetLibrary.does_asset_exist(paths[0]):\n"+
                "    asset=unreal.AssetToolsHelpers.get_asset_tools().duplicate_asset('AS_SRUB_SourceRate',base.rstrip('/'),unreal.load_asset(source))\n"+
                "    assert asset and unreal.UnrealBridgeAnimLibrary.set_anim_sequence_rate_scale(paths[0],2.0)\n"+
                "    created.append(paths[0])\n"+
                "if not unreal.EditorAssetLibrary.does_asset_exist(paths[1]):\n"+
                f"    result=AssetFactory.create_blueprint(path=base.rstrip('/'),name='AN_SRUB_Marker',parent_class_path={NATIVE!r},save=False)\n"+
                "    assert unreal.load_asset(paths[1]),str(result)\n"+
                "    created.append(paths[1])\n"+
                "dirty=list(unreal.UnrealBridgeEditorLibrary.get_dirty_package_names())\n"+
                "assert set(dirty)<=set(paths),dirty\n"+
                "saved=[unreal.EditorAssetLibrary.save_asset(p,only_if_is_dirty=True) for p in created]\n"+
                "assert all(saved),saved\n"+
                "print(json.dumps({'created':created,'dirty':list(unreal.UnrealBridgeEditorLibrary.get_dirty_package_names()),'rate':unreal.load_asset(paths[0]).get_editor_property('rate_scale'),'bp_class':unreal.EditorAssetLibrary.load_blueprint_class(paths[1]).get_path_name()}))")
            h.report['helpers']=helpers; h.check('Owned source RateScale and Blueprint class loaded',helpers['rate']==2 and not helpers['dirty'],helpers)
            def segment(slot,seq,start=0,rate=1,loops=1):
                return dict(op='add_segment',slot_name=slot,sequence_path=seq,source_start_seconds=.1,source_end_seconds=.9,montage_start_seconds=start,play_rate=rate,loop_count=loops)
            initial=[dict(op='create_montage',skeleton_path=SKELETON),dict(op='add_slot',slot_name='UpperBody'),dict(op='add_slot',slot_name='UpperBodyAdditive'),
                segment('UpperBody',BASE+'AS_SRUB_SourceRate',rate=.5),segment('UpperBody',SOURCE,.8),segment('UpperBodyAdditive',ADDITIVE,loops=2)]
            snapshot=h.snapshot(TARGET)
            if not snapshot['targets'][TARGET]['exists']:
                preview=h.author(h.request(TARGET,'anim.montage_segments',initial))
                h.check('Two slots, cropped segments, source RateScale, non-unit rate and loops preview',preview.get('ok') and abs(preview['model']['length_seconds']-1.6)<1e-4,preview)
                h.check('Dry run leaves target absent',not h.snapshot(TARGET)['targets'][TARGET]['exists'])
                request=h.request(TARGET,'anim.montage_segments',initial,False,True)
                applied=h.author(request); h.check('Native guarded create saved exactly target',applied.get('ok') and applied['saved'],applied)
                repeated=h.author(request); h.check('Repeated request did not add segments',repeated==applied,repeated)
            model=h.call('Anim','get_montage_edit_model',TARGET)
            h.report['initial_model']=model
            slots=model['slots']; first=slots[0]['segments']; refs=[dict(slot_name=slots[0]['slot_name'],index=x['index'],fingerprint=x['fingerprint']) for x in first]
            moves=[dict(op='move_segment',segment=refs[0],slot_name='UpperBody',montage_start_seconds=.8),dict(op='move_segment',segment=refs[1],slot_name='UpperBody',montage_start_seconds=0)]
            preview=h.author(h.request(TARGET,'anim.montage_segments',moves)); h.check('Reorder preserves absolute events and lengths in preview',preview.get('ok'),preview)
            applied=h.author(h.request(TARGET,'anim.montage_segments',moves,False,True)); h.check('Both original fingerprints survive batch reorder',applied.get('ok') and applied['saved'],applied)
            updated=h.call('Anim','get_montage_edit_model',TARGET); ref=dict(slot_name='UpperBody',index=0,fingerprint=updated['slots'][0]['segments'][0]['fingerprint'])
            origin=updated['slots'][0]['segments'][0]
            op={key:origin[key] for key in ('sequence_path','source_start_seconds','source_end_seconds','montage_start_seconds','play_rate','loop_count')}; op.update(op='update_segment',segment=ref)
            same=h.author(h.request(TARGET,'anim.montage_segments',[op],False,True)); h.check('Validated update round trip',same.get('ok'),same)
            baseline=h.snapshot(TARGET)['targets'][TARGET]['revision']
            for label,change in [('Zero length',{'source_end_seconds':.1}),('Overlap',{'montage_start_seconds':.1}),('Gap',{'montage_start_seconds':2}),('Wrong source type',{'sequence_path':SKELETON})]:
                bad=dict(op,**change); request=h.request(TARGET,'anim.montage_segments',[bad]); result=h.author(request)
                h.check(label+' rejected before mutation',not result.get('ok') and h.snapshot(TARGET)['targets'][TARGET]['revision']==baseline,result)
            notifies=h.call('Anim','get_notify_edit_model',TARGET)
            # New default animation tracks use the native track name; do not rename them.
            tracks=notifies.get('tracks',[]); track_name=tracks[0] if tracks else 'SRUB'
            entries=[]
            for guid,cls,kind,time,duration,marker in [(GUIDS[0],NATIVE,'notify',.25,0,'Native'),(GUIDS[1],STATE,'state',.35,.3,'Window'),(GUIDS[2],helpers['bp_class'],'notify',1.1,0,'Blueprint')]:
                entries.append(dict(notify_guid=guid,class_path=cls,kind=kind,track_index=0,track_name=track_name,time_seconds=time,duration_seconds=duration,properties={'MarkerId':{'type':'name','value':marker}}))
            existing={x['notify_guid'] for x in notifies['notifies']}
            missing=[x for x in entries if x['notify_guid'] not in existing]
            if missing:
                preview=h.author(h.request(TARGET,'anim.notify_add',missing)); h.check('Native and Blueprint typed Notify preview',preview.get('ok'),preview)
                req=h.request(TARGET,'anim.notify_add',missing,False,True); added=h.author(req)
                h.check('Typed instances saved as animation subobjects',added.get('ok') and added['saved'],added)
                h.check('Notify retry is idempotent',h.author(req)==added)
            updated=h.author(h.request(TARGET,'anim.notify_update',entries,False,True)); h.check('GUID update preserves concrete classes',updated.get('ok') and updated['saved'],updated)
            for label,change in [('Invalid class',{'class_path':'/Script/Engine.Actor'}),('Out of bounds',{'time_seconds':9}),('Wrong property',{'properties':{'Outer':{'type':'name','value':'Bad'}}})]:
                bad=dict(entries[0],**change); result=h.author(h.request(TARGET,'anim.notify_update',[bad])); h.check(label+' Notify rejected',not result.get('ok'),result)
            # Existing Section API remains supported and uses the exact single target save guard.
            section=h.execute('import json,unreal\n'+f'p={TARGET!r}\n'+
                "assert not unreal.UnrealBridgeEditorLibrary.get_dirty_package_names()\n"+
                "m=json.loads(unreal.UnrealBridgeAnimLibrary.get_montage_edit_model(p))\n"+
                "if not any(x['name']=='Second' for x in m['sections']): assert unreal.UnrealBridgeAnimLibrary.add_montage_section(p,'Second',.8)\n"+
                "assert unreal.UnrealBridgeAnimLibrary.set_montage_section_next(p,'Default','Second')\n"+
                "dirty=list(unreal.UnrealBridgeEditorLibrary.get_dirty_package_names()); assert set(dirty)<={p},dirty\n"+
                "assert unreal.EditorAssetLibrary.save_asset(p,only_if_is_dirty=True)\nprint(json.dumps({'saved':True,'dirty':list(unreal.UnrealBridgeEditorLibrary.get_dirty_package_names())}))")
            h.check('Existing Section API saved under exact whitelist',section['saved'] and not section['dirty'],section)
        model=h.call('Anim','get_montage_edit_model',TARGET); notifies=h.call('Anim','get_notify_edit_model',TARGET)
        h.report['final_model']=model; h.report['final_notifies']=notifies
        h.check('Two slots and three segments persist',len(model['slots'])==2 and sum(len(s['segments']) for s in model['slots'])==3,model)
        h.check('Three typed event GUIDs persist',{x['notify_guid'] for x in notifies['notifies']}==set(GUIDS),notifies)
        h.finish()
    except Exception as exc:
        h.report.update(status='failed',error=str(exc)); h.persist(); raise


if __name__=='__main__': main()
