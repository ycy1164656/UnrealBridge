"""Read-only final runtime, generated-contract and exact-source integration check."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
from authoring_live_support import AuthoringLive


def fingerprint(path):
    data=path.read_bytes()
    assert data,path
    return dict(path=str(path.resolve()),bytes=len(data),sha256=hashlib.sha256(data).hexdigest())


def main():
    p=argparse.ArgumentParser(); p.add_argument('--project',required=True); p.add_argument('--out',required=True)
    a=p.parse_args(); h=AuthoringLive(a.project,a.out); sr=Path(a.project).resolve().parent; br=Path(__file__).resolve().parents[1]
    h.report['generated_utc']=datetime.now(timezone.utc).isoformat()
    try:
        health=h.server.bridge_health(project=h.project); h.report['health']=health; h.persist()
        h.check('Current Editor health reports ready',health.get('ready') is True,health)
        actual=h.execute('import json,unreal,unreal_bridge\ns=unreal.UnrealBridgeEditorLibrary.get_editor_state()\n'+
            'print(json.dumps({"pie":s.is_pie,"map":s.current_level_path,"dirty":list(unreal.UnrealBridgeEditorLibrary.get_dirty_package_names()),'+
            '"shader_jobs":unreal.UnrealBridgeEditorLibrary.get_shader_compile_job_count(),"asset_jobs":unreal.UnrealBridgeEditorLibrary.get_asset_compile_job_count(),'+
            '"live_coding_compiling":unreal.UnrealBridgeEditorLibrary.is_live_coding_compiling(),"wrapper_compatible":unreal_bridge.verify_runtime_compatibility(),'+
            '"wrapper_file":unreal_bridge.__file__,"registry_hash":unreal.UnrealBridgeRegistryLibrary.get_tool_registry_hash(),'+
            '"wrapper_manifest_hash":unreal_bridge._MANIFEST_HASH,"engine_version":unreal.SystemLibrary.get_engine_version()}))')
        h.report['runtime']=actual
        h.check('Final Editor is idle and has no Dirty packages',not actual['pie'] and actual['dirty']==[] and actual['shader_jobs']==actual['asset_jobs']==0 and not actual['live_coding_compiling'],actual)
        h.check('Original project map remains loaded',actual['map']=='/ShooterRoyal/Maps/SR_GamePlay_Field',actual)
        master=br/'Plugin/UnrealBridge'; mirror=sr/'Plugins/UnrealBridge'
        for name in ('unreal_bridge.py','bridge_manifest_meta.json'):
            x=fingerprint(master/'Content/Python'/name); y=fingerprint(mirror/'Content/Python'/name)
            h.report.setdefault('generated_files',[]).append(dict(master=x,mirror=y))
            h.check(name+' exact master/project mirror matches',x['sha256']==y['sha256'])
        manifest=json.loads((br/'.claude/skills/unreal-bridge/scripts/bridge_manifest.json').read_text(encoding='utf-8'))
        meta=json.loads((mirror/'Content/Python/bridge_manifest_meta.json').read_text(encoding='utf-8'))
        h.report['manifest']=dict(hash=manifest['manifest_hash'],registry_hash=meta['registry_hash'],plugin_version=meta['plugin_version'])
        h.check('Native registry, loaded wrapper and manifest agree',actual['wrapper_compatible'] and actual['registry_hash']==meta['registry_hash'] and actual['wrapper_manifest_hash']==manifest['manifest_hash']==meta['manifest_hash'],h.report['manifest'])
        h.check('Loaded wrapper belongs to this project',Path(actual['wrapper_file']).resolve()==(mirror/'Content/Python/unreal_bridge.py').resolve(),actual['wrapper_file'])
        records=[]
        for path in sorted((master/'Source').rglob('*')):
            if path.suffix not in ('.h','.cpp','.cs'): continue
            copied=mirror/path.relative_to(master)
            if not copied.exists(): raise AssertionError(('Native mirror missing',str(copied)))
            left=fingerprint(path); right=fingerprint(copied)
            records.append(dict(relative=path.relative_to(master).as_posix(),sha256=left['sha256'],mirror_sha256=right['sha256']))
        h.report['native_source_mirrors']=records
        h.check('All native plugin source mirrors match',all(x['sha256']==x['mirror_sha256'] for x in records),len(records))
        h.report['binaries']=[fingerprint(mirror/'Binaries/Win64/UnrealEditor-UnrealBridge.dll'),fingerprint(sr/'Plugins/GameFeatures/ShooterRoyal/Binaries/Win64/UnrealEditor-ShooterRoyalRuntime.dll')]
        h.report['scope']='Read-only final state and integration proof; no asset saves, rollback, live process shutdown or human acceptance'
        h.finish()
    except Exception as exc:
        h.report.update(status='failed',error=str(exc)); h.persist(); raise


if __name__=='__main__': main()
