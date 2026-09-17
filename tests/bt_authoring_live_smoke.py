"""Incremental BT graph/runtime authoring and negative cases on exact owned assets."""
import argparse
from authoring_live_support import AuthoringLive

BASE='/ShooterRoyal/Automation/BridgeUpgrade/AI/'
TARGET=BASE+'BT_SRUB_Authoring'
BB=BASE+'BB_SRUB_Authoring'
BP=BASE+'BTT_SRUB_Pursue'
G={name:f'276070000000000000000000000000{i:02d}' for i,name in enumerate(['root','selector','sequence','pursue','idle','decorator','service','wait'],1)}


def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--project',required=True); parser.add_argument('--out',required=True); parser.add_argument('--reopen-check',action='store_true')
    args=parser.parse_args(); h=AuthoringLive(args.project,args.out)
    try:
        h.check('Clean unused Editor baseline',h.editor()=={'pie':False,'dirty':[]})
        if not args.reopen_check:
            if not h.snapshot(BB)['targets'][BB]['exists']:
                ops=[dict(op='add_key',key_name=n,key_type=t) for n,t in [('HasTarget','bool'),('Destination','vector'),('ServiceTicks','int')]]
                preview=h.author(h.request(BB,'ai.blackboard_keys',ops)); h.check('Typed Blackboard preview',preview.get('ok'),preview)
                result=h.author(h.request(BB,'ai.blackboard_keys',ops,False,True)); h.check('Only declared Blackboard saved',result.get('ok') and result['saved'],result)
            bp=h.execute('import json,unreal\nfrom unreal_bridge import AssetFactory\n'+f'p={BP!r}\n'+
                'assert not unreal.UnrealBridgeEditorLibrary.get_dirty_package_names()\n'+
                "if not unreal.EditorAssetLibrary.does_asset_exist(p):\n"+
                f"    AssetFactory.create_blueprint(path={BASE.rstrip('/')!r},name='BTT_SRUB_Pursue',parent_class_path='/Script/ShooterRoyalRuntime.SRBTTask_AutomationProbe',save=False)\n"+
                'dirty=list(unreal.UnrealBridgeEditorLibrary.get_dirty_package_names()); assert set(dirty)<={p},dirty\n'+
                'if dirty: assert unreal.EditorAssetLibrary.save_asset(p,only_if_is_dirty=True)\n'+
                "print(json.dumps({'class_path':unreal.EditorAssetLibrary.load_blueprint_class(p).get_path_name(),'dirty':list(unreal.UnrealBridgeEditorLibrary.get_dirty_package_names())}))")
            h.report['blueprint']=bp; h.check('Owned Blueprint task class compiled and saved',bool(bp['class_path']) and not bp['dirty'],bp)
            def node(name,kind,cls,x,y,properties=None): return dict(op='add_node',node_guid=G[name],kind=kind,class_path=cls,x=x,y=y,properties=properties or {})
            def connect(parent,child): return dict(op='connect',parent_guid=G[parent],child_guid=G[child])
            ops=[dict(op='create_tree',blackboard_path=BB,root_guid=G['root']),node('selector','composite','/Script/AIModule.BTComposite_Selector',0,100),
                node('sequence','composite','/Script/AIModule.BTComposite_Sequence',240,300),node('pursue','task',bp['class_path'],480,300,{'MarkerId':{'type':'name','value':'Pursue'}}),
                node('idle','task','/Script/ShooterRoyalRuntime.SRBTTask_AutomationProbe',-240,300,{'MarkerId':{'type':'name','value':'Idle'}}),
                node('wait','task','/Script/AIModule.BTTask_Wait',240,500,{'WaitTime':{'type':'seconds','value':.05},'RandomDeviation':{'type':'seconds','value':0}}),
                connect('root','selector'),connect('selector','idle'),connect('selector','sequence'),connect('selector','pursue'),connect('sequence','wait'),
                dict(op='attach_decorator',node_guid=G['decorator'],parent_guid=G['sequence'],class_path='/Script/AIModule.BTDecorator_Blackboard',properties={
                    'BlackboardKey':{'type':'blackboard_key','value':{'key_name':'HasTarget','key_type':'bool'}},'BasicOperation':{'type':'enum','value':'Set'},'FlowAbortMode':{'type':'enum','value':'Both'}}),
                dict(op='attach_service',node_guid=G['service'],parent_guid=G['selector'],class_path='/Script/ShooterRoyalRuntime.SRBTService_AutomationProbe',properties={'Interval':{'type':'seconds','value':.1},'RandomDeviation':{'type':'seconds','value':0}})]
            if not h.snapshot(TARGET)['targets'][TARGET]['exists']:
                preview=h.author(h.request(TARGET,'ai.behavior_tree',ops)); h.check('Schema-based full graph preview builds runtime tree',preview.get('ok'),preview)
                h.check('BT dry run leaves target absent',not h.snapshot(TARGET)['targets'][TARGET]['exists'])
                req=h.request(TARGET,'ai.behavior_tree',ops,False,True); applied=h.author(req)
                h.check('Composite Task Decorator Service saved together',applied.get('ok') and applied['saved'],applied)
                h.check('Repeated graph request keeps node GUIDs',h.author(req)==applied)
            # Final authoring layout: move the pursuit task into the conditional sequence.
            edit=[dict(op='reparent',parent_guid=G['sequence'],child_guid=G['pursue']),
                dict(op='reorder_children',parent_guid=G['selector'],children=[G['sequence'],G['idle']]),
                dict(op='reorder_children',parent_guid=G['sequence'],children=[G['wait'],G['pursue']]),
                dict(op='set_location',node_guid=G['root'],x=0,y=-100),
                dict(op='set_property',node_guid=G['wait'],properties={'WaitTime':{'type':'seconds','value':.07}})]
            current=h.call('AI','get_behavior_tree_edit_model',TARGET)
            if next(x for x in current['nodes'] if x['node_guid']==G['pursue'])['parent_guid']!=G['sequence']:
                preview=h.author(h.request(TARGET,'ai.behavior_tree',edit)); h.check('Preview contains old edges and final child order',preview.get('ok') and 'previous_model' in preview['model'],preview)
                result=h.author(h.request(TARGET,'ai.behavior_tree',edit,False,True)); h.check('Reparent and reorder retain every node',result.get('ok') and result['saved'] and len(result['model']['nodes'])==8,result)
            revision=h.snapshot(TARGET)['targets'][TARGET]['revision']
            negatives=[('Cycle',dict(op='reparent',parent_guid=G['sequence'],child_guid=G['selector'])),
                ('Wrong key type',dict(op='set_property',node_guid=G['decorator'],properties={'BlackboardKey':{'type':'blackboard_key','value':{'key_name':'HasTarget','key_type':'float'}}})),
                ('Unknown task class',dict(node('pursue','task','/Script/Engine.Actor',0,0),node_guid='27607900000000000000000000000001')),
                ('Unsupported composite semantics',dict(node('sequence','composite','/Script/AIModule.BTComposite_SimpleParallel',0,0),node_guid='27607900000000000000000000000002')),
                ('Wrong parent kind',dict(op='reparent',parent_guid=G['idle'],child_guid=G['pursue'])),
                ('Unsafe service interval',dict(op='set_property',node_guid=G['service'],properties={'Interval':{'type':'seconds','value':0}})),
                ('Missing child in reorder',dict(op='reorder_children',parent_guid=G['selector'],children=[G['sequence']]))]
            for label,bad in negatives:
                result=h.author(h.request(TARGET,'ai.behavior_tree',[bad])); h.check(label+' rejected without mutation',not result.get('ok') and h.snapshot(TARGET)['targets'][TARGET]['revision']==revision,result)
        model=h.call('AI','validate_behavior_tree_asset',TARGET); h.report['final_model']=model
        h.check('Saved graph and runtime child order agree',model.get('ok') and len(model['nodes'])==8,model)
        nodes={x['node_guid']:x for x in model['nodes']}
        h.check('Conditional sequence has final execution priority',nodes[G['selector']]['children']==[G['sequence'],G['idle']],model)
        h.check('Blueprint task remains inside conditional sequence',nodes[G['pursue']]['runtime_class'].startswith(BP) and nodes[G['pursue']]['parent_guid']==G['sequence'],model)
        h.finish()
    except Exception as exc:
        h.report.update(status='failed',error=str(exc)); h.persist(); raise


if __name__=='__main__': main()
