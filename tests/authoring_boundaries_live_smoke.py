"""Real unrelated Dirty retention, target Dirty refusal and foreign skeleton."""
import argparse
import hashlib
from pathlib import Path
from authoring_live_support import AuthoringLive
from anim_authoring_live_smoke import TARGET, BASE, SKELETON


def main():
    p=argparse.ArgumentParser(); p.add_argument('--project',required=True); p.add_argument('--out',required=True)
    a=p.parse_args(); h=AuthoringLive(a.project,a.out); sentinel=BASE+'AS_SRUB_SourceRate'; created_dirty=False
    try:
        h.check('Clean Editor baseline',h.editor()=={'pie':False,'dirty':[]})
        model=h.call('Anim','get_montage_edit_model',TARGET); slot=model['slots'][0]; segment=slot['segments'][0]
        op={k:segment[k] for k in ('sequence_path','source_start_seconds','source_end_seconds','montage_start_seconds','play_rate','loop_count')}
        op.update(op='update_segment',segment=dict(slot_name=slot['slot_name'],index=segment['index'],fingerprint=segment['fingerprint']))
        foreign=h.execute('import json,unreal\n'+f'wanted={SKELETON!r}\n'+
            "r=unreal.AssetRegistryHelpers.get_asset_registry(); entries=r.get_assets_by_class(unreal.TopLevelAssetPath('/Script/Engine','AnimSequence'),False)\n"+
            "found=None; loaded=0\n"+
            "for entry in entries:\n"+
            "    tag=str(entry.get_tag_value('Skeleton'))\n"+
            "    if not tag or tag=='None' or wanted in tag: continue\n"+
            "    asset=entry.get_asset(); loaded+=1\n"+
            "    skeleton=asset.get_editor_property('skeleton') if isinstance(asset,unreal.AnimSequence) else None\n"+
            "    if skeleton and skeleton.get_path_name()!=wanted:\n"+
            "        found={'asset':asset.get_path_name(),'class':asset.get_class().get_path_name(),'skeleton':skeleton.get_path_name()}; break\n"+
            "    if loaded>=16: break\n"+
            "print(json.dumps({'foreign':found,'candidates_loaded':loaded}))")
        h.check('A real animation uses a different skeleton',bool(foreign['foreign']),foreign)
        h.report['foreign_skeleton']=foreign
        before=h.snapshot(TARGET)['targets'][TARGET]['revision']
        refused=h.author(h.request(TARGET,'anim.montage_segments',[dict(op,sequence_path=foreign['foreign']['asset'])]))
        h.check('Actual foreign skeleton refuses before target mutation',not refused.get('ok') and h.snapshot(TARGET)['targets'][TARGET]['revision']==before,refused)
        disk=Path(a.project).parent/'Plugins/GameFeatures/ShooterRoyal/Content/Automation/BridgeUpgrade/Animation/AS_SRUB_SourceRate.uasset'
        disk_hash=hashlib.sha256(disk.read_bytes()).hexdigest()
        # Mark our own sentinel package dirty without editing any property.
        # It is unrelated to the subsequent single-target authoring request.
        marked=h.execute('import json,unreal\n'+f'a=unreal.load_asset({sentinel!r}); a.modify(True)\n'+
            'print(json.dumps({"dirty":sorted(unreal.UnrealBridgeEditorLibrary.get_dirty_package_names())}))')
        created_dirty=True
        h.check('Declared sentinel supplies real pre-existing unrelated Dirty',marked['dirty']==[sentinel],marked)
        applied=h.author(h.request(TARGET,'anim.montage_segments',[op],False,True))
        h.check('Target authoring saves while preserving unrelated Dirty',applied.get('ok') and applied['saved'] and h.editor()['dirty']==[sentinel],applied)
        h.check('Unrelated asset file was never saved or rewritten',hashlib.sha256(disk.read_bytes()).hexdigest()==disk_hash)
        marked=h.execute('import json,unreal\n'+f'unreal.load_asset({TARGET!r}).modify(True)\n'+
            'print(json.dumps({"dirty":sorted(unreal.UnrealBridgeEditorLibrary.get_dirty_package_names())}))')
        request=h.request(TARGET,'anim.montage_segments',[op],False,True); result=h.author(request)
        h.check('Target pre-existing Dirty refuses without clearing either package',not result.get('ok') and h.editor()['dirty']==sorted([TARGET,sentinel]),result)
    except Exception as exc:
        h.report.update(status='failed',error=str(exc)); h.persist(); raise
    finally:
        if created_dirty:
            cleanup=h.execute('import json,unreal\n'+f'allowed={[TARGET,sentinel]!r}\n'+
                'dirty=list(unreal.UnrealBridgeEditorLibrary.get_dirty_package_names())\nassert set(dirty)<=set(allowed),dirty\n'+
                'saved={p:unreal.EditorAssetLibrary.save_asset(p,only_if_is_dirty=True) for p in dirty}\n'+
                'print(json.dumps({"saved":saved,"dirty":list(unreal.UnrealBridgeEditorLibrary.get_dirty_package_names())}))')
            h.report['cleanup']=cleanup; h.persist()
    h.finish()


if __name__=='__main__': main()
