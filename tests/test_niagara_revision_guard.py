"""Revision conflicts must fail before any transaction or setter executes."""
import json
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'skills/unreal-bridge/scripts'))
from unreal_bridge_niagara import EMITTER_REVISION_FIELDS,emitter_revision_guard_code,require_unique_module

class RevisionGuardTests(unittest.TestCase):
    def setUp(self):
        self.actual={k:k+'-current' for k in EMITTER_REVISION_FIELDS}
        self.conditions=[dict(system_path='/ShooterRoyal/Tests/S.S',emitter_name='A',emitter_handle_id='handle',expected_revision=dict(self.actual))]
        self.emitters=[SimpleNamespace(name='A',handle_id='handle')]
        library=SimpleNamespace(get_niagara_system_structure=lambda p:SimpleNamespace(emitters=self.emitters),
            inspect_niagara_emitter_instance=lambda *args:json.dumps(dict(self.actual,ok=True)))
        self.env={'json':json,'unreal':SimpleNamespace(UnrealBridgeNiagaraLibrary=library)}
    def run_guard(self):exec(emitter_revision_guard_code(self.conditions),self.env)
    def test_same_revision_is_accepted(self):self.run_guard()
    def test_each_changed_scope_is_rejected(self):
        for field in EMITTER_REVISION_FIELDS:
            original=self.actual[field];self.actual[field]='changed'
            with self.assertRaisesRegex(RuntimeError,'NIAGARA_REVISION_CONFLICT'):self.run_guard()
            self.actual[field]=original
    def test_replaced_and_duplicate_emitters_are_rejected(self):
        self.emitters[0].handle_id='replacement'
        with self.assertRaisesRegex(RuntimeError,'NIAGARA_IDENTITY_CONFLICT'):self.run_guard()
        self.emitters[0].handle_id='handle';self.emitters.append(self.emitters[0])
        with self.assertRaisesRegex(RuntimeError,'NIAGARA_IDENTITY_CONFLICT'):self.run_guard()
    def test_missing_revision_scope_is_rejected(self):
        self.conditions[0]['expected_revision'].pop('rapid_iteration_hash')
        with self.assertRaises(ValueError):self.run_guard()
    def test_duplicate_module_names_and_missing_module_refuse_resolution(self):
        module={'moduleName':'SpawnRate'}
        self.assertEqual(require_unique_module({'returnValue':{'modules':[module]}},'SpawnRate'),module)
        for modules in [[],[module,module]]:
            with self.assertRaisesRegex(ValueError,'NIAGARA_MODULE_IDENTITY_CONFLICT'):
                require_unique_module({'returnValue':{'modules':modules}},'SpawnRate')
if __name__=='__main__':unittest.main()
