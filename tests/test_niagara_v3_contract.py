from __future__ import annotations

import ast
import importlib.util
import json
import sys
import unittest
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
SCRIPT_DIR = REPO / ".claude" / "skills" / "unreal-bridge" / "scripts"
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))


def load(name: str):
    path = SCRIPT_DIR / f"{name}.py"
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    assert spec and spec.loader
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


domains = load("unreal_bridge_domains")
niagara = load("unreal_bridge_niagara")
CATALOG = json.loads((SCRIPT_DIR / "official_tool_catalog.json").read_text(encoding="utf-8"))
REGISTRY = domains.OfficialDomainRegistry(CATALOG)


class NiagaraBuildersTests(unittest.TestCase):
    SYSTEM = "/Game/VFX/NS_Test.NS_Test"

    def test_object_path_to_package_is_stable(self) -> None:
        self.assertEqual(
            "/Game/VFX/NS_Test",
            niagara.package_from_object_path(self.SYSTEM),
        )
        self.assertEqual(
            "/ShooterRoyal/VFX/NS_Test",
            niagara.package_from_object_path(
                "NiagaraSystem'/ShooterRoyal/VFX/NS_Test.NS_Test'"
            ),
        )

    def test_module_reference_matches_official_schema(self) -> None:
        reference = niagara.stack_item_reference(
            self.SYSTEM,
            emitter_name="Emitter_A",
            script_name="ParticleUpdateScript",
            module_name="Scale Sprite Size",
        )
        operation = REGISTRY.resolve("niagara", "GetModuleInputValues")
        self.assertEqual([], REGISTRY.validate(operation, {"moduleRef": reference}))

    def test_user_parameter_matches_official_schema(self) -> None:
        call = niagara.add_user_variables_call(
            self.SYSTEM,
            [
                {
                    "name": "User.ParticleScale",
                    "type_ref_path": "/Script/Niagara.NiagaraFloat",
                    "default_value": {"value": 1.0},
                    "description": "Runtime particle scale",
                }
            ],
        )
        operation = REGISTRY.resolve("niagara", call["operation"])
        self.assertEqual([], REGISTRY.validate(operation, call["arguments"]))

    def test_linked_and_dynamic_inputs_match_official_schema(self) -> None:
        reference = niagara.stack_item_reference(
            self.SYSTEM,
            emitter_name="Emitter_A",
            script_name="ParticleUpdateScript",
            module_name="Scale Sprite Size",
            input_name_stack=["Scale Factor"],
        )
        operation = REGISTRY.resolve("niagara", "SetStackInputData")
        linked = niagara.set_stack_input_call(
            reference,
            niagara.linked_input_data(
                "ParticleScale", "/Script/Niagara.NiagaraFloat"
            ),
        )
        dynamic = niagara.set_stack_input_call(
            reference,
            niagara.dynamic_input_data(
                "/Niagara/DefaultAssets/DynamicInputs/DI_Test.DI_Test"
            ),
        )
        self.assertEqual([], REGISTRY.validate(operation, linked["arguments"]))
        self.assertEqual([], REGISTRY.validate(operation, dynamic["arguments"]))

    def test_rename_uses_emitter_handle_property_json(self) -> None:
        call = niagara.rename_emitter_call(self.SYSTEM, "Old", "New")
        operation = REGISTRY.resolve("niagara", "SetEmitterData")
        self.assertEqual([], REGISTRY.validate(operation, call["arguments"]))
        values = json.loads(call["arguments"]["emitterData"]["propertyValues"])
        self.assertEqual({"Name": "New"}, values)

    def test_existing_binding_guard_is_conservative(self) -> None:
        linked = {
            "returnValue": {
                "struct": {"refPath": niagara.LINKED_INPUT_STRUCT},
                "value": {},
            }
        }
        literal = {
            "returnValue": {
                "struct": {"refPath": "/Script/Niagara.NiagaraFloat"},
                "value": {"value": 1.0},
            }
        }
        self.assertEqual("Linked", niagara.existing_binding_kind(linked))
        self.assertEqual("Literal", niagara.existing_binding_kind(literal))
        self.assertEqual("Unknown", niagara.existing_binding_kind({}))


class NiagaraMcpSurfaceTests(unittest.TestCase):
    def test_all_eleven_true_v3_interfaces_are_real_functions(self) -> None:
        source = (SCRIPT_DIR / "unreal_bridge_mcp_server.py").read_text(
            encoding="utf-8"
        )
        tree = ast.parse(source)
        functions = {
            node.name for node in tree.body if isinstance(node, ast.FunctionDef)
        }
        expected = {
            "get_niagara_module_inputs",
            "get_niagara_input_binding_tree",
            "list_niagara_dynamic_input_scripts",
            "ensure_niagara_user_parameters",
            "rename_niagara_emitter",
            "rename_niagara_user_parameter",
            "bind_niagara_module_input_to_user_parameter",
            "attach_niagara_dynamic_input",
            "set_niagara_dynamic_input_sub_input",
            "get_niagara_compile_diagnostics",
            "configure_niagara_particle_controls",
        }
        self.assertEqual(expected, expected & functions)

    def test_batch_native_contract_is_exposed(self) -> None:
        header = (
            REPO
            / "Plugin"
            / "UnrealBridge"
            / "Source"
            / "UnrealBridge"
            / "Public"
            / "UnrealBridgeUE58Library.h"
        ).read_text(encoding="utf-8")
        self.assertIn("ExecuteOfficialTransactionalToolsetBatch", header)
        self.assertIn("NiagaraSystemPathToCompile", header)
        self.assertIn("NiagaraUserParameterRenamesJson", header)
        self.assertIn("ToolSaveBehavior = \"Never\"", header)

    def test_high_level_workflows_compile_and_read_back_inside_transaction(self) -> None:
        source = (SCRIPT_DIR / "unreal_bridge_mcp_server.py").read_text(
            encoding="utf-8"
        )
        self.assertIn('"compile_niagara_system_path": system_path', source)
        self.assertIn('"allow_readbacks": True', source)
        self.assertIn('"niagara_user_parameter_renames"', source)
        self.assertNotIn(
            '_niagara_read_step(\n                "compile_state",\n'
            '                "GetSystemCompileState"',
            source,
        )


if __name__ == "__main__":
    unittest.main()
