import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'.claude/skills/unreal-bridge/scripts'))
import unreal_bridge_pointer_input as pointer


def request():
    return dict(schema=pointer.SCHEMA,request_id='click-1',world_handle='ubr:world:w',widget_handle='ubr:widget-input:a',
        generation='a'*32,window_id='window:a',geometry_revision='b'*40,local_player_index=0,coordinate_space='widget_normalized',
        button='left',modifiers=[],events=[dict(type=kind,x=.5,y=.5,at_seconds=index*.1) for index,kind in enumerate(('move','down','up'))])


class PointerTests(unittest.TestCase):
    def test_accepts_edge_and_retains_exact_coordinates(self):
        value=request(); value['events'][0].update(x=0,y=1)
        original=copy.deepcopy(value); self.assertEqual(value,pointer.normalize(value)); self.assertEqual(original,value)

    def test_rejects_nonfinite_bool_and_out_of_bounds(self):
        for field,invalid in [('x',float('nan')),('y',float('inf')),('x',True),('x',1.001),('y',-1),('at_seconds',11)]:
            value=request(); value['events'][1][field]=invalid
            with self.subTest(field=field,invalid=invalid),self.assertRaises(ValueError): pointer.normalize(value)

    def test_rejects_unbalanced_and_reordered_sequences(self):
        for events in [[{'type':'down','x':0,'y':0,'at_seconds':0}],request()['events'][:-1],list(reversed(request()['events']))]:
            value=request(); value['events']=events
            with self.assertRaises(ValueError): pointer.normalize(value)

    def test_bounds_schema_scope_and_modifiers(self):
        for change in ({'local_player_index':True},{'local_player_index':4},{'world_handle':'Editor'}, {'generation':'old'},
                       {'modifiers':['shift','shift']},{'events':request()['events']*41},{'script':'print(1)'},{'button':'wheel'}):
            with self.subTest(change=change),self.assertRaises(ValueError): pointer.normalize(dict(request(),**change))


if __name__=='__main__': unittest.main()
