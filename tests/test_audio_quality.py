"""Regression for previously invisible graph changes and permissive literals."""
import ast
import base64
import copy
import json
from pathlib import Path
import textwrap
import unittest
from typing import Any, Dict, List

SOURCE = Path(__file__).resolve().parents[1]/'skills/unreal-bridge/scripts/unreal_bridge_mcp_server.py'

def load_helpers():
    tree=ast.parse(SOURCE.read_text(encoding='utf-8'))
    wanted={'_validate_metasound_graph_ops','_metasound_graph_intent_code'}
    nodes=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in wanted]
    nodes += [n for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id in {'_METASOUND_GRAPH_FIELDS','_METASOUND_GRAPH_OPS'} for t in n.targets)]
    scope=dict(base64=base64,json=json,textwrap=textwrap,Any=Any,Dict=Dict,List=List)
    exec(compile(ast.Module(body=nodes,type_ignores=[]),str(SOURCE),'exec'),scope)
    return scope

class AudioQualityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.scope=load_helpers()
        code=cls.scope['_metasound_graph_intent_code']({})
        wanted={'_diff','_names','_node_ids'}
        nodes=[n for n in ast.parse(code).body if isinstance(n,ast.FunctionDef) and n.name in wanted]
        cls.diff_scope={'json':json}
        exec(compile(ast.Module(body=nodes,type_ignores=[]),'diff','exec'),cls.diff_scope)

    def test_invalid_literals_rejected_before_submission(self):
        for kind,values in {'Float':['nan','Infinity','1e999','abc','3.5e38','1.0junk'], 'Int':['1.5','2147483648','-2147483649','1x'], 'Bool':['yes','1','False']}.items():
            for value in values:
                with self.subTest(kind=kind,value=value),self.assertRaises(ValueError):
                    self.scope['_validate_metasound_graph_ops']([dict(op='set_default',dst_ref='a',dst_input='x',literal_type=kind,value=value)])

    def test_valid_literals(self):
        for kind,value in [('Float','1e-3'),('Float','-.25'),('Int','-2147483648'),('Bool','false'),('String',''),('Object',''),('Default','')]:
            self.assertEqual(value,self.scope['_validate_metasound_graph_ops']([dict(op='set_default',dst_ref='a',dst_input='x',literal_type=kind,value=value)])[0]['value'])

    def test_rewire_with_same_edge_count_and_literal_changes(self):
        for section in ('edges','pins','page_defaults','variables'):
            before={'total_edge_count':1,'details':{'complete':True,section:[{'id':'same','value':'before'}]}}
            after=copy.deepcopy(before);after['details'][section][0]['value']='after'
            result=self.diff_scope['_diff'](before,after)
            self.assertEqual(0,result['edge_delta']);self.assertTrue(result['semantic_changed'])
            self.assertEqual(1,len(result['semantic_diff'][section]['added']))

    def test_order_is_not_semantics_and_truncation_is_unknown(self):
        before={'details':{'complete':True,'edges':[{'id':'a'},{'id':'b'}]}}
        after=copy.deepcopy(before);after['details']['edges'].reverse()
        self.assertFalse(self.diff_scope['_diff'](before,after)['semantic_changed'])
        after['details']['complete']=False
        self.assertIsNone(self.diff_scope['_diff'](before,after)['semantic_changed'])

if __name__=='__main__':unittest.main()
