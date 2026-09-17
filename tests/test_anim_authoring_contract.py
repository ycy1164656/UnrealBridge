import copy
from pathlib import Path
import sys
import unittest
from types import SimpleNamespace
from unittest.mock import Mock
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'.claude/skills/unreal-bridge/scripts'))
import unreal_bridge_authoring as authoring


def request():
    target='/ShooterRoyal/Automation/BridgeUpgrade/Animation/AM_SRUB_Segments'
    context=dict(schema='unrealbridge.upgrade.v1',operation_id='upgrade.validate',request_id='anim-1',project_identity='C:/Project/ShooterRoyal.uproject',
        editor_session_id='editor',engine_version='5.8.2',target_packages=[target],expected_revisions={target:'absent'},dry_run=True,save_policy='never',world_handle='',timeout_seconds=30)
    return dict(schema=authoring.SCHEMA,operation_id='anim.montage_segments',context=context,save_policy='never',
        operations=[{'op':'create_montage','skeleton_path':'/ShooterRoyal/Skeleton'}, {'op':'add_slot','slot_name':'DefaultSlot'},
        dict(op='add_segment',slot_name='DefaultSlot',sequence_path='/ShooterRoyal/Animation',source_start_seconds=0,source_end_seconds=1,montage_start_seconds=0,play_rate=1,loop_count=1)])


class AnimAuthoringTests(unittest.TestCase):
    def test_preview_keeps_the_existing_no_write_context_unchanged(self):
        value=request(); original=copy.deepcopy(value)
        self.assertTrue(authoring.normalize(value)['context']['dry_run']); self.assertEqual(original,value)

    def test_delete_arbitrary_scripts_and_invalid_rates_never_dispatch(self):
        server=SimpleNamespace(bridge_submit_job=Mock())
        variants=[]
        for rate in (0,-1,float('nan'),True):
            value=request(); value['operations'][2]['play_rate']=rate; variants.append(value)
        value=request(); value['operations']=[{'op':'delete_segment'}]; variants.append(value)
        value=request(); value['script']='run()'; variants.append(value)
        for value in variants:
            with self.assertRaises(ValueError): authoring.submit(server,value)
        server.bridge_submit_job.assert_not_called()

    def test_save_is_separate_explicit_scope(self):
        value=request(); value['save_policy']='declared_targets'
        with self.assertRaises(ValueError): authoring.normalize(value)
        value['context']['dry_run']=False
        self.assertEqual('declared_targets',authoring.normalize(value)['save_policy'])
        value['context']['target_packages'].append('/ShooterRoyal/Other')
        with self.assertRaises(ValueError): authoring.normalize(value)

    def test_semantic_preview_selects_native_entry_and_stable_job_identity(self):
        server=SimpleNamespace(bridge_submit_job=Mock(return_value={'job_id':'job'}))
        result=authoring.submit(server,request()); self.assertEqual('job',result['job_id'])
        args=server.bridge_submit_job.call_args
        self.assertIn('preview_montage_segment_ops',args.args[0]); compile(args.args[0],'<authoring>','exec')
        self.assertEqual('authoring:editor:anim-1',args.kwargs['idempotency_key'])


if __name__=='__main__': unittest.main()
