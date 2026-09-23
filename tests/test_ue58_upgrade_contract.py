from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
AUDIT_PATH = REPO / "tools" / "audit_ue58_toolsets.py"
POLICY_PATH = REPO / "tools" / "ue58_toolset_policy.json"
ACCESS_BUILDER_PATH = REPO / "tools" / "build_ue58_access_policy.py"

spec = importlib.util.spec_from_file_location("audit_ue58_toolsets", AUDIT_PATH)
audit_module = importlib.util.module_from_spec(spec)
assert spec and spec.loader
sys.modules[spec.name] = audit_module
spec.loader.exec_module(audit_module)

access_spec = importlib.util.spec_from_file_location(
    "build_ue58_access_policy", ACCESS_BUILDER_PATH
)
access_module = importlib.util.module_from_spec(access_spec)
assert access_spec and access_spec.loader
sys.modules[access_spec.name] = access_module
access_spec.loader.exec_module(access_module)


class UE58UpgradeContractTests(unittest.TestCase):
    def test_plugin_and_protocol_versions(self) -> None:
        descriptor = json.loads(
            (REPO / "Plugin" / "UnrealBridge" / "UnrealBridge.uplugin").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(descriptor["Version"], 5)
        self.assertEqual(descriptor["VersionName"], "3.2.1")

        version_header = (
            REPO
            / "Plugin"
            / "UnrealBridge"
            / "Source"
            / "UnrealBridge"
            / "Public"
            / "UnrealBridgeVersion.h"
        ).read_text(encoding="utf-8")
        self.assertIn('Plugin = TEXT("3.2.1")', version_header)
        self.assertIn("Protocol = 2", version_header)

        dependencies = {entry["Name"]: entry for entry in descriptor["Plugins"]}
        for dependency in ("ToolsetRegistry", "AllToolsets"):
            self.assertTrue(dependencies[dependency]["Enabled"])
            self.assertTrue(dependencies[dependency]["Optional"])

    def test_ue58_dependency_is_conditionally_compiled(self) -> None:
        build_rules = (
            REPO
            / "Plugin"
            / "UnrealBridge"
            / "Source"
            / "UnrealBridge"
            / "UnrealBridge.Build.cs"
        ).read_text(encoding="utf-8")
        self.assertIn("UNREALBRIDGE_WITH_UE58_TOOLSET_REGISTRY", build_rules)
        self.assertIn('PrivateDependencyModuleNames.Add("ToolsetRegistry")', build_rules)
        self.assertIn("Target.Version.MinorVersion >= 8", build_rules)

    def test_manifest_generator_preserves_federation_metadata(self) -> None:
        generator = (REPO / "tools" / "gen_manifest.py").read_text(encoding="utf-8")
        self.assertIn('"provider", "engine_min"', generator)
        registry = (
            REPO
            / "Plugin"
            / "UnrealBridge"
            / "Source"
            / "UnrealBridge"
            / "Private"
            / "UnrealBridgeRegistryLibrary.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn('TEXT("provider")', registry)
        self.assertIn('TEXT("engine_min")', registry)

    def test_stdio_mcp_dependency_is_pinned_below_v2(self) -> None:
        config_example = (
            REPO / ".claude" / "skills" / "unreal-bridge" / "SKILL.md"
        ).read_text(encoding="utf-8")
        self.assertIn("mcp>=1.6.0,<2", config_example)

    def test_audit_is_conservative(self) -> None:
        policy = json.loads(POLICY_PATH.read_text(encoding="utf-8"))
        catalog = [
            {
                "name": "Fixture",
                "version": "1",
                "tools": [
                    {
                        "name": "Fixture.GetSnapshot",
                        "annotations": {"readOnlyHint": True, "destructiveHint": False},
                        "inputSchema": {"type": "object"},
                    },
                    {
                        "name": "Fixture.UpdateThing",
                        "annotations": {"readOnlyHint": False, "destructiveHint": False},
                    },
                    {"name": "Fixture.DeleteEverything"},
                    {"name": "Fixture.QueryWithoutAnnotation"},
                ],
            }
        ]
        audit = audit_module.normalize_catalog(catalog, policy, engine_version="5.8.1")
        tools = {item["tool"]: item for item in audit["toolsets"][0]["tools"]}
        self.assertEqual(audit["toolsets"][0]["module"], "Fixture")
        self.assertEqual(tools["GetSnapshot"]["classification"], "Reuse")
        self.assertEqual(tools["GetSnapshot"]["bridge_execution"], "Allowed")
        self.assertEqual(tools["GetSnapshot"]["inferred_side_effects"], "ReadOnlyDeclared")
        self.assertEqual(tools["UpdateThing"]["classification"], "Wrap")
        self.assertTrue(tools["UpdateThing"]["bridge_execution"].startswith("Blocked"))
        self.assertEqual(tools["DeleteEverything"]["classification"], "Reject")
        self.assertEqual(tools["QueryWithoutAnnotation"]["classification"], "Extend")
        self.assertEqual(tools["QueryWithoutAnnotation"]["inferred_side_effects"], "ReadOnlyCandidate")

    def test_runtime_policy_is_exact_complete_and_deny_by_default(self) -> None:
        audit = json.loads(
            (REPO / "tests" / "fixtures" / "ue58-toolsets.json").read_text(
                encoding="utf-8"
            )
        )
        policy, catalog = access_module.build_policy(audit)
        self.assertTrue(policy["deny_by_default"])
        self.assertEqual(policy["tool_count"], audit["tool_count"])
        self.assertEqual(sum(policy["access_counts"].values()), audit["tool_count"])
        self.assertEqual(catalog["tool_count"], audit["tool_count"])
        self.assertTrue(
            all(len(entry["schema_sha256"]) == 64 for entry in policy["tools"].values())
        )

        def access(toolset: str, tool: str) -> str:
            return policy["tools"][f"{toolset}|{tool}"]["access"]

        self.assertEqual(
            access("NiagaraToolsets.NiagaraToolset_System", "GetModuleInputValues"),
            "ReadOnly",
        )
        self.assertEqual(
            access("NiagaraToolsets.NiagaraToolset_System", "SetStackInputData"),
            "TransactionalSync",
        )
        self.assertEqual(
            access("SlateInspectorToolset.SlateInspectorToolset", "Click"),
            "RuntimeInteraction",
        )
        self.assertEqual(
            access("AutomationTestToolset.AutomationTestToolset", "RunTests"),
            "RuntimeInteraction",
        )
        self.assertEqual(
            access("NiagaraToolsets.NiagaraToolset_System", "RemoveModule"),
            "Rejected",
        )
        self.assertEqual(
            access(
                "animation_toolset.toolsets.controlrig.ControlRigTools",
                "create",
            ),
            "Rejected",
        )
        self.assertEqual(
            access(
                "editor_toolset.toolsets.scene.SceneTools",
                "commit_level_instance",
            ),
            "Rejected",
        )
        self.assertEqual(
            access("editor_toolset.toolsets.asset.AssetTools", "is_dirty"),
            "ReadOnly",
        )
        self.assertEqual(
            access("editor_toolset.toolsets.asset.AssetTools", "exists"),
            "ReadOnly",
        )
        self.assertEqual(
            access(
                "animation_toolset.toolsets.controlrig.ControlRigTools",
                "import_bones_from_asset",
            ),
            "TransactionalSync",
        )
        self.assertEqual(
            access(
                "animation_toolset.toolsets.import_export.SequencerImportExportTools",
                "export_anim_sequence",
            ),
            "TransactionalSync",
        )
        self.assertEqual(
            access(
                "editor_toolset.toolsets.scene.SceneTools",
                "edit_level_instance",
            ),
            "RuntimeInteraction",
        )
        self.assertEqual(
            access("UMGToolSet.UMGToolSet", "CompileWidgetBlueprint"),
            "TransactionalSync",
        )
        self.assertEqual(
            access(
                "editor_toolset.toolsets.programmatic.ProgrammaticToolset",
                "execute_tool_script",
            ),
            "Rejected",
        )

    def test_python_source_audit_rejects_decorated_tools_that_save(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            source = (
                Path(temp_dir)
                / "FixtureToolset"
                / "Content"
                / "Python"
                / "fixture"
                / "toolsets"
                / "asset.py"
            )
            source.parent.mkdir(parents=True)
            source.write_text(
                """
class FixtureTools:
    @toolset_registry.tool_call
    @staticmethod
    def create(path: str):
        unreal.EditorAssetLibrary.save_asset(path)

    @toolset_registry.tool_call
    @staticmethod
    def inspect(path: str):
        return unreal.EditorAssetLibrary.does_asset_exist(path)
""",
                encoding="utf-8",
            )
            findings = access_module.audit_python_tool_sources(Path(temp_dir))

        self.assertEqual(
            findings,
            {
                "fixture.toolsets.asset.FixtureTools|create": [
                    "unreal.EditorAssetLibrary.save_asset"
                ]
            },
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
