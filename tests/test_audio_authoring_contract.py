import unittest
from test_anim_authoring_contract import request
import unreal_bridge_authoring as authoring


class AudioAuthoringTests(unittest.TestCase):
    def example(self):
        value=request(); value['operation_id']='audio.sound_cue'; value['operations']=[dict(op='create_cue')]; return value
    def test_sound_cue_explicit_executor(self):
        self.assertEqual('audio.sound_cue',authoring.normalize(self.example())['operation_id'])
    def test_unsupported_nodes_do_not_autoconvert_to_metasound(self):
        value=self.example(); value['operations']=[dict(op='add_node',node_guid='1'*32,kind='arbitrary_sound_node',input_count=0,x=0,y=0,properties={})]
        with self.assertRaises(ValueError): authoring.normalize(value)
    def test_no_delete_script_or_blanket_graph_replacement(self):
        for name in ('delete_node','clear_graph','replace_graph','python'):
            value=self.example(); value['operations']=[dict(op=name)]
            with self.assertRaises(ValueError): authoring.normalize(value)
    def test_nonfinite_and_fractional_pins_rejected(self):
        for index in (float('nan'),.5,True):
            value=self.example(); value['operations']=[dict(op='connect',source_guid='1'*32,destination_guid='2'*32,input_index=index)]
            with self.assertRaises(ValueError): authoring.normalize(value)


if __name__=='__main__': unittest.main()
