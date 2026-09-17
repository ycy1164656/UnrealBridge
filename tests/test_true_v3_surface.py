from __future__ import annotations

import ast
import json
import sys
import unittest
from collections import Counter
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
SCRIPT_DIR = REPO / ".claude" / "skills" / "unreal-bridge" / "scripts"
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import unreal_bridge_domains as domains  # noqa: E402


CATALOG = json.loads(
    (SCRIPT_DIR / "official_tool_catalog.json").read_text(encoding="utf-8")
)
REGISTRY = domains.OfficialDomainRegistry(CATALOG)


class TrueV3McpSurfaceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = (SCRIPT_DIR / "unreal_bridge_mcp_server.py").read_text(
            encoding="utf-8"
        )
        tree = ast.parse(cls.source)
        cls.functions = {
            node.name: node for node in tree.body if isinstance(node, ast.FunctionDef)
        }

    def test_complete_discovery_artifact_scenario_and_automation_surface(self) -> None:
        expected = {
            "bridge_list_domains",
            "bridge_search_tools",
            "bridge_describe_tools",
            "bridge_read_artifact",
            "bridge_submit_scenario",
            "bridge_get_scenario",
            "bridge_cancel_scenario",
            "bridge_submit_automation_run",
            "bridge_cancel_automation_run",
        }
        self.assertEqual(expected, expected & self.functions.keys())

    def test_high_level_graph_workflows_are_real_mcp_functions(self) -> None:
        expected = {
            "bridge_submit_typed_domain_workflow",
            "bridge_submit_control_rig_workflow",
            "bridge_submit_sequencer_workflow",
            "bridge_submit_pcg_workflow",
            "bridge_submit_physics_workflow",
            "bridge_submit_dataflow_workflow",
            "bridge_submit_mesh_workflow",
            "bridge_submit_material_graph_intent",
            "bridge_submit_metasound_graph_intent",
        }
        self.assertEqual(expected, expected & self.functions.keys())
        self.assertIn("allow_readbacks", self.source)
        self.assertIn("contains_readback", (
            REPO
            / "Plugin"
            / "UnrealBridge"
            / "Source"
            / "UnrealBridge"
            / "Private"
            / "UnrealBridgeUE58Library.cpp"
        ).read_text(encoding="utf-8"))

    def test_graph_domains_have_both_read_and_transactional_operations(self) -> None:
        for domain in (
            "control_rig",
            "sequencer",
            "pcg",
            "physics",
            "dataflow",
            "mesh",
        ):
            counts = Counter(record["access"] for record in REGISTRY.list(domain))
            self.assertGreater(counts["ReadOnly"], 0, domain)
            self.assertGreater(counts["TransactionalSync"], 0, domain)

    def test_preview_is_the_default_for_every_high_level_graph_writer(self) -> None:
        for name in (
            "bridge_submit_typed_domain_workflow",
            "bridge_submit_control_rig_workflow",
            "bridge_submit_sequencer_workflow",
            "bridge_submit_pcg_workflow",
            "bridge_submit_physics_workflow",
            "bridge_submit_dataflow_workflow",
            "bridge_submit_mesh_workflow",
            "bridge_submit_material_graph_intent",
            "bridge_submit_metasound_graph_intent",
        ):
            node = self.functions[name]
            defaults = node.args.defaults
            names = [argument.arg for argument in node.args.args]
            default_by_name = dict(zip(names[-len(defaults) :], defaults))
            self.assertIn("apply", default_by_name, name)
            self.assertIsInstance(default_by_name["apply"], ast.Constant, name)
            self.assertIs(default_by_name["apply"].value, False, name)

    def test_native_graph_intents_finalize_before_restored_readback(self) -> None:
        self.assertGreaterEqual(
            self.source.count("finalize_change_set(_change_set_id, False)"), 4
        )
        self.assertGreaterEqual(
            self.source.count("finalize_change_set(_change_set_id, True)"), 2
        )

    def test_native_structs_are_recursively_json_serialized(self) -> None:
        call_code = ast.get_source_segment(
            self.source, self.functions["_call_code"]
        )
        self.assertIsNotNone(call_code)
        assert call_code is not None
        self.assertIn('hasattr(value, "to_dict")', call_code)
        self.assertIn('{"Array", "Set"}', call_code)
        self.assertIn('type(value).__name__ == "Map"', call_code)

    def test_game_feature_retry_key_is_scenario_scoped(self) -> None:
        feature_code = ast.get_source_segment(
            self.source, self.functions["bridge_submit_game_feature_transition"]
        )
        self.assertIsNotNone(feature_code)
        assert feature_code is not None
        self.assertIn('"run_data_validation"', feature_code)
        self.assertNotIn('"b_run_data_validation"', feature_code)
        self.assertIn("uuid.uuid4().hex", feature_code)
        self.assertIn("transition_idempotency_key", feature_code)


class TrueV3NativeSurfaceTests(unittest.TestCase):
    def _header(self, name: str) -> str:
        return (
            REPO
            / "Plugin"
            / "UnrealBridge"
            / "Source"
            / "UnrealBridge"
            / "Public"
            / name
        ).read_text(encoding="utf-8")

    def test_blueprint_resolver_and_runtime_umg_are_exposed(self) -> None:
        blueprint = self._header("UnrealBridgeBlueprintLibrary.h")
        umg = self._header("UnrealBridgeUMGLibrary.h")
        self.assertIn("ResolveBlueprintComponent", blueprint)
        for token in (
            "local_scs",
            "local_cdo",
            "inherited_scs",
            "ich_override",
            "parent_fallback",
        ):
            self.assertIn(token, blueprint)
        self.assertIn("GetRuntimeWidgetTree", umg)
        self.assertIn("GetRuntimeWidgetState", umg)
        for token in (
            "bHasKeyboardFocus",
            "bHasUserFocus",
            "AbsolutePosition",
            "AbsoluteSize",
            "bEnabled",
        ):
            self.assertIn(token, umg)

    def test_state_tree_has_evaluator_condition_parameter_binding_and_runtime(self) -> None:
        header = self._header("UnrealBridgeStateTreeLibrary.h")
        for token in (
            "AddStateTreeEvaluator",
            "AddStateTreeGlobalTask",
            "AddStateTreeEnterCondition",
            "AddStateTreeTransitionCondition",
            "AddStateTreeParameter",
            "AddStateTreeBinding",
            "SetStateTreeNodeInstanceProperty",
            "GetRuntimeStateTrees",
        ):
            self.assertIn(token, header)

    def test_ai_game_feature_network_world_and_audio_depth_is_native(self) -> None:
        expected = {
            "UnrealBridgeAILibrary.h": (
                "GetRuntimeBehaviorTrees",
                "GetRuntimeEQSQueries",
                "GetRuntimePerception",
            ),
            "UnrealBridgeGameFeatureLibrary.h": (
                "ListGameFeatures",
                "GetGameFeatureInfo",
            ),
            "UnrealBridgeNetworkingLibrary.h": (
                "AuditNetworkClass",
                "AuditNetworkActor",
                "GetNetworkWorldSnapshots",
            ),
            "UnrealBridgeWorldPartitionLibrary.h": (
                "GetWorldPartitionSnapshots",
                "ValidateWorldPartitionRuntime",
            ),
            "UnrealBridgeAudioLibrary.h": (
                "GetMetaSoundGraphInfo",
                "ApplyMetaSoundGraphOps",
                "GetRuntimeAudioComponents",
            ),
        }
        for name, tokens in expected.items():
            header = self._header(name)
            for token in tokens:
                self.assertIn(token, header, f"{name}: {token}")


if __name__ == "__main__":
    unittest.main(verbosity=2)
