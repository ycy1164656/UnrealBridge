from __future__ import annotations

import importlib.util
import json
import sys
import unittest
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
MODULE_PATH = (
    REPO
    / ".claude"
    / "skills"
    / "unreal-bridge"
    / "scripts"
    / "unreal_bridge_domains.py"
)
spec = importlib.util.spec_from_file_location("unreal_bridge_domains", MODULE_PATH)
domains = importlib.util.module_from_spec(spec)
assert spec and spec.loader
sys.modules[spec.name] = domains
spec.loader.exec_module(domains)


CATALOG = json.loads(
    (
        REPO
        / ".claude"
        / "skills"
        / "unreal-bridge"
        / "scripts"
        / "official_tool_catalog.json"
    ).read_text(encoding="utf-8")
)


class DomainRegistryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.registry = domains.OfficialDomainRegistry(CATALOG)

    def test_every_official_operation_is_indexed_once(self) -> None:
        self.assertEqual(832, len(self.registry.operations))
        self.assertEqual(832, len(self.registry.by_id))

    def test_required_true_3_domains_exist(self) -> None:
        available = {record["domain"] for record in self.registry.domains()}
        self.assertTrue(
            {
                "ai",
                "automation",
                "blueprint",
                "control_rig",
                "data_registry",
                "dataflow",
                "game_feature",
                "gas",
                "material",
                "niagara",
                "pcg",
                "physics",
                "sequencer",
                "state_tree",
                "ui",
                "validation",
                "world",
            }.issubset(available)
        )

    def test_exact_read_only_overrides_are_not_transactional(self) -> None:
        snapshot = self.registry.resolve("ui", "Snapshot")
        windows = self.registry.resolve("slate", "Windows")
        enum_info = self.registry.resolve("niagara", "UEnum_Info")
        self.assertEqual("ReadOnly", snapshot.access)
        self.assertEqual("ReadOnly", windows.access)
        self.assertEqual("ReadOnly", enum_info.access)

    def test_ambiguous_short_name_requires_exact_id(self) -> None:
        with self.assertRaisesRegex(KeyError, "ambiguous"):
            self.registry.resolve("niagara", "GetUserVariables")
        exact = self.registry.resolve(
            "niagara",
            "NiagaraToolsets.NiagaraToolset_System|GetUserVariables",
        )
        self.assertEqual("NiagaraToolsets.NiagaraToolset_System", exact.toolset)

    def test_schema_validation_rejects_missing_and_unknown_fields(self) -> None:
        operation = self.registry.resolve("niagara", "SetStackInputData")
        missing = self.registry.validate(operation, {})
        self.assertTrue(any("inputData is required" in item for item in missing))
        self.assertTrue(any("stackInputRef is required" in item for item in missing))

        get_summary = self.registry.resolve("niagara", "GetSystemSummary")
        unknown = self.registry.validate(
            get_summary, {"system": {"refPath": "/Game/VFX/NS_Test.NS_Test"}, "typo": 1}
        )
        self.assertTrue(any("typo is not allowed" in item for item in unknown))

    def test_rejected_operation_remains_discoverable_but_not_executable(self) -> None:
        remove = self.registry.resolve("niagara", "RemoveModule")
        self.assertEqual("Rejected", remove.access)
        listed = self.registry.list("niagara", access="Rejected")
        self.assertIn(remove.id, {record["id"] for record in listed})

    def test_listed_operation_can_be_resolved_verbatim(self) -> None:
        listed = self.registry.list("automation")
        self.assertTrue(listed)
        for record in listed:
            self.assertEqual(record["id"], record["operation"])
            self.assertEqual(
                record["id"],
                self.registry.resolve("automation", record["operation"]).id,
            )


class StructuralSchemaTests(unittest.TestCase):
    def test_nested_required_enum_and_additional_properties(self) -> None:
        schema = {
            "type": "object",
            "additionalProperties": False,
            "required": ["items"],
            "properties": {
                "items": {
                    "type": "array",
                    "minItems": 1,
                    "items": {
                        "type": "object",
                        "required": ["mode"],
                        "additionalProperties": False,
                        "properties": {"mode": {"type": "string", "enum": ["A", "B"]}},
                    },
                }
            },
        }
        self.assertEqual(
            [],
            domains.validate_json_schema({"items": [{"mode": "A"}]}, schema),
        )
        errors = domains.validate_json_schema(
            {"items": [{"mode": "C", "extra": True}]}, schema
        )
        self.assertTrue(any("must be one of" in item for item in errors))
        self.assertTrue(any("extra is not allowed" in item for item in errors))


if __name__ == "__main__":
    unittest.main()
