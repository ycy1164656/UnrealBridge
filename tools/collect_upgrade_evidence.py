"""Read-only source/asset inventory for the partial non-ARMOURY upgrade.

Writes diagnostic JSON only. A source hash or an old passing report never
promotes a capability to integrated/loaded status; native acceptance is separate.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path


def fingerprint(path):
    data=path.read_bytes()
    if not data: raise ValueError(f'Empty evidence input: {path}')
    return dict(path=str(path.resolve()),bytes=len(data),sha256=hashlib.sha256(data).hexdigest())


def main():
    p=argparse.ArgumentParser(); p.add_argument('--project',required=True); a=p.parse_args()
    sr=Path(a.project).resolve().parent; br=Path(__file__).resolve().parents[1]
    native=br/'Plugin/UnrealBridge/Source/UnrealBridge'; mirror=sr/'Plugins/UnrealBridge/Source/UnrealBridge'
    runtime=sr/'Plugins/GameFeatures/ShooterRoyal/Source/ShooterRoyalRuntime'
    names={'UnrealBridge.Build.cs','Public/UnrealBridgeVersion.h','Private/UnrealBridgeUE58Library.cpp','Public/UnrealBridgeAnimLibrary.h','Public/UnrealBridgeAILibrary.h','Public/UnrealBridgeAudioLibrary.h',
           'Public/UnrealBridgeChangeSetLibrary.h','Private/UnrealBridgeChangeSetLibrary.cpp','Private/UnrealBridgeModule.cpp'}
    for path in native.rglob('*'):
        if path.suffix in ('.h','.cpp') and any(term in path.name for term in ('Upgrade','NetworkSession','Authoring','AudioRouting','AudioSession','SlateInput')):
            names.add(path.relative_to(native).as_posix())
    source=[]
    descriptor=fingerprint(br/'Plugin/UnrealBridge/UnrealBridge.uplugin'); descriptor_mirror=fingerprint(sr/'Plugins/UnrealBridge/UnrealBridge.uplugin')
    source.append(dict(descriptor,kind='bridge_native',relative='../UnrealBridge.uplugin',mirror=descriptor_mirror,mirror_matches=descriptor['sha256']==descriptor_mirror['sha256']))
    for name in sorted(names):
        record=fingerprint(native/name); copied=fingerprint(mirror/name)
        record.update(kind='bridge_native',relative=name,mirror=copied,mirror_matches=record['sha256']==copied['sha256']); source.append(record)
    for path in sorted(runtime.rglob('*')):
        if path.suffix not in ('.h','.cpp','.cs'): continue
        body=path.read_text(encoding='utf-8-sig')
        if any(term in body for term in ('SRAutomation','SRAnimationAuthoring','SRBehaviorTreeAuthoring','SRInputAutomation')) or path.name in ('SRGameMode.h','SRGameMode.cpp','SRMinimapPanelWidgetBase.cpp','ShooterRoyalRuntime.Build.cs'):
            source.append(dict(fingerprint(path),kind='shooterroyal_related_source',relative=path.relative_to(runtime).as_posix()))
    scripts=br/'.claude/skills/unreal-bridge/scripts'
    for name in ('unreal_bridge_upgrade.py','unreal_bridge_authoring.py','unreal_bridge_pointer_input.py','unreal_bridge_audio_sessions.py',
                 'unreal_bridge_sessions.py','unreal_bridge_external_mcp.py','unreal_bridge_network_sessions.py','unreal_bridge_mcp_server.py',
                 'project_adapters/shooterroyal_scenarios.py'):
        source.append(dict(fingerprint(scripts/name),kind='bridge_host',relative=name))
    assets=[]; content=sr/'Plugins/GameFeatures/ShooterRoyal/Content'
    for path in sorted((content/'Automation/BridgeUpgrade').rglob('*.uasset')):
        assets.append(dict(fingerprint(path),package='/ShooterRoyal/'+path.relative_to(content).with_suffix('').as_posix()))
    evidence_root=sr/'.tmp/artifacts/unrealbridge-upgrade'; reports=[]
    for package in sorted(evidence_root.glob('SRUB-*-Windows-Development')):
        for path in sorted((package/'latest').glob('*.json')):
            if path.name in ('capabilities-v1.json','native-sync.json'): continue
            data=json.loads(path.read_text(encoding='utf-8-sig'))
            if not isinstance(data,dict): continue
            assertions=data.get('assertions')
            reports.append(dict(fingerprint(path),package=package.name,status=data.get('status',data.get('result','not_a_test_report')),
                                assertion_count=len(assertions) if isinstance(assertions,list) else None))
    doc=json.loads((evidence_root/'Docs-Windows-Static/latest/doc-audit.json').read_text(encoding='utf-8-sig'))
    hashes='\n'.join(item['path']+' '+item['sha256'] for item in source)
    def passed(package,name):
        path=evidence_root/f'SRUB-{package:02d}-Windows-Development/latest/{name}.json'
        return path.exists() and json.loads(path.read_text(encoding='utf-8-sig')).get('status')=='passed'
    matrix={'schema':'shooterroyal.unrealbridge.capabilities.v1','generated_utc':datetime.now(timezone.utc).isoformat(),
        'state':'3.1.0_partial_foreground_acceptance_pending','source_digest_sha256':hashlib.sha256(hashes.encode()).hexdigest(),
        'source_inventory':source,'assets':assets,'reports':reports,'document_audit':doc,
        'generated_manifest':fingerprint(scripts/'bridge_manifest.json'),
        'capabilities':{
            'scenario_framework':'native_lifecycle_and_final_runtime_regression_pass' if passed(11,'native-automation') and passed(3,'gameplay-four') else 'implemented_bounded_runtime_evidence_final_regression_pending',
            'deterministic_gameplay':'combined_and_individual_runtime_pass' if passed(3,'gameplay-four') else 'implemented_individual_evidence_combined_regression_pending',
            'pie_and_editor_game_network':'bounded_runtime_evidence_including_real_rejoin_and_cancel',
            'pointer_coordinates':'full_size_dpi_drag_deployment_runtime_pass' if passed(5,'pointer-matrix') else '3.1_no_foreground_activation_implemented_desktop_input_acceptance_pending',
            'montage_and_typed_notify':'authoring_playback_reopen_and_dirty_skeleton_boundaries_pass' if passed(11,'authoring-boundaries') else 'authoring_playback_reopen_pass_foreign_skeleton_boundary_pending',
            'behavior_tree':'selector_sequence_and_adapted_nodes_pass_boundaries_verified' if passed(7,'boundaries') else 'selector_sequence_and_adapted_nodes_pass_final_boundary_regression_pending',
            'sound_cue_and_routing':'authoring_reopen_pass_temporary_submix_cycle_lifetime_fixed',
            'audio_mix_runtime':'pre_3.1_visible_runtime_22_pass_3.1_foreground_audio_acceptance_pending' if passed(8,'audio-runtime') else 'runtime_verification_pending',
            'mcp_discovery':'new_and_legacy_tools_verified_over_stdio_native_execution_separate',
            'mcp_native_execution':'3.1_stdio_version_and_actual_native_call_pass' if passed(11,'mcp-native') else 'final_stdio_native_execution_pending',
            'shipping':'actual_shipping_target_and_six_machine_code_exclusion_guards_pass_cooked_runtime_unverified' if passed(11,'shipping-exclusion') else 'shipping_build_and_exclusion_probe_pending',
            'armoury':'deferred_by_user'},
        'limitations':['No completed non-ARMOURY upgrade marker is issued.',
            'Version 3.1.0 removes automatic desktop window activation. Foreground-dependent tests are deferred while the user is using the computer.',
            'Background verification uses RenderOffScreen and NoSound; it is not desktop input, window DPI or audible playback acceptance.',
            'This collector does not query live PIE/Dirty state; consult the timestamped final-state report.',
            'Normal integration and Shipping builds succeeded; runtime capability reports are evaluated separately from build success.',
            'Launcher packaged Dedicated Server, SimpleParallel authoring, unknown SoundCue nodes and complex Submix effect chains are not covered.',
            'Dialogue-source mixing and human audio/visual/gameplay acceptance remain unverified.']}
    out=evidence_root/'SRUB-11-Windows-Development/latest/capabilities-v1.json'; out.parent.mkdir(parents=True,exist_ok=True)
    out.write_text(json.dumps(matrix,ensure_ascii=False,indent=2),encoding='utf8')
    native_records=[x for x in source if x['kind']=='bridge_native']
    print(json.dumps({'state':matrix['state'],'source_files':len(source),'native_mirrors':len(native_records),
        'all_mirrors_match':all(x['mirror_matches'] for x in native_records),'saved_asset_files':len(assets),'reports':len(reports),'out':str(out)}))


if __name__=='__main__': main()
