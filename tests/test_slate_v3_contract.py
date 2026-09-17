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

spec = importlib.util.spec_from_file_location(
    "unreal_bridge_domains", SCRIPT_DIR / "unreal_bridge_domains.py"
)
domains = importlib.util.module_from_spec(spec)
assert spec and spec.loader
sys.modules[spec.name] = domains
spec.loader.exec_module(domains)

CATALOG = json.loads((SCRIPT_DIR / "official_tool_catalog.json").read_text(encoding="utf-8"))
REGISTRY = domains.OfficialDomainRegistry(CATALOG)


class SlatePolicyTests(unittest.TestCase):
    def test_semantic_actions_have_expected_execution_planes(self) -> None:
        for name in ("Click", "Type", "Drag", "SelectOption", "WaitFor", "Screenshot"):
            operation = REGISTRY.resolve("ui", name)
            self.assertEqual("RuntimeInteraction", operation.access, name)
        for name in ("Snapshot", "Windows", "ListObservers"):
            operation = REGISTRY.resolve("ui", name)
            self.assertEqual("ReadOnly", operation.access, name)

    def test_action_schemas_reject_wrong_shapes(self) -> None:
        click = REGISTRY.resolve("ui", "Click")
        self.assertEqual([], REGISTRY.validate(click, {"ref": "button:Play"}))
        self.assertTrue(REGISTRY.validate(click, {}))
        drag = REGISTRY.resolve("ui", "Drag")
        self.assertEqual(
            [],
            REGISTRY.validate(drag, {"startRef": "a", "endRef": "b"}),
        )


class SlateMcpSurfaceTests(unittest.TestCase):
    def test_workflow_and_action_tools_exist(self) -> None:
        source = (SCRIPT_DIR / "unreal_bridge_mcp_server.py").read_text(
            encoding="utf-8"
        )
        tree = ast.parse(source)
        functions = {
            node.name for node in tree.body if isinstance(node, ast.FunctionDef)
        }
        self.assertIn("bridge_submit_slate_action", functions)
        self.assertIn("bridge_submit_slate_workflow", functions)
        self.assertIn("bridge_compare_golden_image", functions)

    def test_workflow_explicitly_reports_no_os_injection(self) -> None:
        source = (SCRIPT_DIR / "unreal_bridge_mcp_server.py").read_text(
            encoding="utf-8"
        )
        self.assertIn('input_injection": "Unreal-Slate-only"', source)
        self.assertIn("os_input_injection=False", source)


if __name__ == "__main__":
    unittest.main()
