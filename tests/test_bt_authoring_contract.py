import copy
import unittest
from types import SimpleNamespace
from unittest.mock import Mock
from test_anim_authoring_contract import request
import unreal_bridge_authoring as authoring


class BTAuthoringTests(unittest.TestCase):
    def example(self):
        value=request(); value['operation_id']='ai.behavior_tree'
        value['operations']=[dict(op='add_node',node_guid='1'*32,kind='task',class_path='/Script/AIModule.BTTask_Wait',x=0,y=100,properties={'WaitTime':{'type':'seconds','value':.2}})]
        return value
    def test_correct_executor_and_preview_scope(self):
        server=SimpleNamespace(bridge_submit_job=Mock(return_value={'job_id':'job'}))
        authoring.submit(server,self.example())
        self.assertIn('preview_behavior_tree_ops',server.bridge_submit_job.call_args.args[0])
    def test_delete_replace_implicit_scripts_and_nonfinite_locations_rejected(self):
        for op in [dict(op='remove_node'),dict(op='replace_tree'),dict(op='set_location',node_guid='1'*32,x=float('nan'),y=0),dict(op='set_property',node_guid='1'*32,properties={},python='mutate()')]:
            value=self.example(); value['operations']=[op]
            with self.assertRaises(ValueError): authoring.normalize(value)
    def test_reorder_requires_unique_bounded_explicit_children(self):
        value=self.example(); value['operations']=[dict(op='reorder_children',parent_guid='1'*32,children=['2'*32,'2'*32])]
        with self.assertRaises(ValueError): authoring.normalize(value)
        value['operations'][0]['children']=['2'*32,'3'*32]
        self.assertEqual(value['operations'],authoring.normalize(copy.deepcopy(value))['operations'])
    def test_blackboard_types_are_not_arbitrary_classes(self):
        value=self.example(); value['operation_id']='ai.blackboard_keys'; value['operations']=[dict(op='add_key',key_name='Target',key_type='object')]
        self.assertEqual(value['operations'],authoring.normalize(copy.deepcopy(value))['operations'])
        value['operations'][0]['key_type']='python_class'
        with self.assertRaises(ValueError): authoring.normalize(value)


if __name__=='__main__': unittest.main()
