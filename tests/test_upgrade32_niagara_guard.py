import importlib.util
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch


class NiagaraGuardTests(unittest.TestCase):
    def setUp(self):
        self.emitters=[SimpleNamespace(name='same',handle_id='guid-a')]
        self.params=[SimpleNamespace(name='User.Radius',type_name='NiagaraFloat',is_data_interface=False,is_u_object=False)]
        fake=SimpleNamespace(UnrealBridgeNiagaraLibrary=SimpleNamespace(
            get_niagara_system_structure=lambda target:SimpleNamespace(emitters=self.emitters),
            get_niagara_user_parameters=lambda target:self.params))
        path=Path(__file__).resolve().parents[1]/'Plugin/UnrealBridge/Content/Python/unreal_bridge_content_ops.py'
        spec=importlib.util.spec_from_file_location('ub32_native_guard_test',path)
        self.module=importlib.util.module_from_spec(spec)
        with patch.dict(sys.modules,{'unreal':fake}):spec.loader.exec_module(self.module)
        self.step={'expected_emitters':{'same':'guid-a'},'values':{'User.Radius':.35}}

    def test_duplicate_or_replaced_emitter_rejected_before_any_mutation(self):
        self.assertEqual(len(self.module._content_niagara_float_variables('/Test/System',self.step)),1)
        self.emitters.append(SimpleNamespace(name='same',handle_id='guid-b'))
        with self.assertRaisesRegex(ValueError,'emitter identity'):
            self.module._content_niagara_float_variables('/Test/System',self.step)
        self.emitters[:]=[SimpleNamespace(name='same',handle_id='guid-replaced')]
        with self.assertRaisesRegex(ValueError,'emitter identity'):
            self.module._content_niagara_float_variables('/Test/System',self.step)

    def test_data_interface_object_and_non_float_rejected(self):
        for field,value in [('is_data_interface',True),('is_u_object',True),('type_name','DynamicInput')]:
            original=getattr(self.params[0],field);setattr(self.params[0],field,value)
            with self.assertRaisesRegex(ValueError,'replacement is forbidden'):
                self.module._content_niagara_float_variables('/Test/System',self.step)
            setattr(self.params[0],field,original)


if __name__=='__main__':unittest.main()
