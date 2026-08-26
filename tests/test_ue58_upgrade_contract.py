from __future__ import annotations

import importlib.util
import json
import sys
import unittest
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
AUDIT_PATH = REPO / "tools" / "audit_ue58_toolsets.py"
POLICY_PATH = REPO / "tools" / "ue58_toolset_policy.json"

spec = importlib.util.spec_from_file_location("audit_ue58_toolsets", AUDIT_PATH)
audit_module = importlib.util.module_from_spec(spec)
assert spec and spec.loader
sys.modules[spec.name] = audit_module
spec.loader.exec_module(audit_module)


class UE58UpgradeContractTests(unittest.TestCase):
    def test_plugin_and_protocol_versions(self) -> None:
        descriptor = json.loads(
            (REPO / "Plugin" / "UnrealBridge" / "UnrealBridge.uplugin").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(descriptor["Version"], 3)
        self.assertEqual(descriptor["VersionName"], "3.0.0")

        version_header = (
            REPO
            / "Plugin"
            / "UnrealBridge"
            / "Source"
            / "UnrealBridge"
            / "Public"
            / "UnrealBridgeVersion.h"
        ).read_text(encoding="utf-8")
        self.assertIn('Plugin = TEXT("3.0.0")', version_header)
        self.assertIn("Protocol = 2", version_header)

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


if __name__ == "__main__":
    unittest.main(verbosity=2)
