import copy
import unittest
from test_anim_authoring_contract import request
import unreal_bridge_authoring as authoring


class AudioRoutingContract(unittest.TestCase):
    def example(self):
        value=request(); value['operation_id']='audio.routing'; target=value['context']['target_packages'][0]
        value['operations']=[dict(op='sound_class_defaults',target=target,gain=dict(unit='db',value=-6),pitch_ratio=1)]
        return value

    def test_audio_can_declare_parent_and_child_in_same_guard(self):
        value=self.example(); value['context']['target_packages'].append('/ShooterRoyal/Automation/Parent')
        value['context']['expected_revisions']['/ShooterRoyal/Automation/Parent']='absent'
        self.assertEqual(2,len(authoring.normalize(value)['context']['target_packages']))
        value['operation_id']='audio.sound_cue'; value['operations']=[dict(op='create_cue')]
        with self.assertRaises(ValueError): authoring.normalize(value)

    def test_every_operation_target_must_be_in_guard(self):
        value=self.example(); value['operations'][0]['target']='/ShooterRoyal/Undeclared'
        with self.assertRaises(ValueError): authoring.normalize(value)

    def test_unit_conversion_is_explicit_and_bounded(self):
        for gain in (.5,dict(unit='milliseconds',value=1),dict(unit='linear',value=-1),dict(unit='db',value=13),dict(unit='linear',value=float('nan'))):
            value=self.example(); value['operations'][0]['gain']=gain
            with self.assertRaises(ValueError): authoring.normalize(value)

    def test_no_profile_global_restore_or_implicit_delete(self):
        for op in ('load_profile','save_profile','set_global_mix','restore_snapshot','delete_asset'):
            value=self.example(); value['operations'][0]['op']=op
            with self.assertRaises(ValueError): authoring.normalize(value)


if __name__=='__main__': unittest.main()
