import json
from pathlib import Path
import tempfile
import unittest
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'skills/unreal-bridge/scripts'))
from unreal_bridge_imports import ImportOrder

class ImportOrderTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.order=ImportOrder(self.temp.name,'fixture')
        self.request={'schema':'unrealbridge.external_import.v1','request_id':'fixture','source_sha256':'abc','target':'/Game/Test'}
        self.order.freeze(self.request)

    def test_identical_request_is_idempotent(self):
        self.assertEqual(self.order.freeze(self.request),self.order.get())

    def test_source_options_cannot_change_under_identity(self):
        with self.assertRaises(ValueError):self.order.freeze({**self.request,'source_sha256':'changed'})

    def test_no_dispatch_without_prepared_contract(self):
        with self.assertRaises(ValueError):self.order.dispatch_intent()

    def test_lost_response_intent_cannot_replay(self):
        self.order.update(contract={'contract_hash':'frozen'},phase='prepared')
        self.order.dispatch_intent()
        with self.assertRaises(ValueError):self.order.dispatch_intent()

    def test_tampered_saved_contract_rejected(self):
        self.order.update(contract={'contract_hash':'frozen'},phase='prepared')
        state=self.order.get();state['contract']['contract_hash']='tampered'
        self.order.path.write_text(json.dumps(state),encoding='utf-8')
        with self.assertRaises(ValueError):self.order.get()

if __name__=='__main__':unittest.main()
