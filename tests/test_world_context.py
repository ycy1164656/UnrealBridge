from __future__ import annotations

import importlib.util
import json
import sys
import types
import unittest
from pathlib import Path
from unittest.mock import Mock, patch


PATH = Path(__file__).resolve().parents[1] / "Plugin/UnrealBridge/Content/Python/unreal_bridge_world_context.py"
spec = importlib.util.spec_from_file_location("world_context_under_test", PATH)
scope = importlib.util.module_from_spec(spec)
assert spec and spec.loader
spec.loader.exec_module(scope)


class WorldScopeTests(unittest.TestCase):
    def setUp(self):
        self.world = Mock()
        self.world.begin_world_scope.return_value = json.dumps({"ok": True, "scope_token": "scope-1"})
        self.world.end_world_scope.return_value = json.dumps({"ok": True})
        self.module = types.ModuleType("unreal_bridge")
        self.module.World = self.world
        self.patch = patch.dict(sys.modules, {"unreal_bridge": self.module})
        self.patch.start()
        self.addCleanup(self.patch.stop)

    def test_exit_closes_the_exact_native_scope(self):
        with scope.world_scope("world-1") as entered:
            self.assertEqual(entered["scope_token"], "scope-1")
        self.world.begin_world_scope.assert_called_once_with(world_handle="world-1")
        self.world.end_world_scope.assert_called_once_with(scope_token="scope-1")

    def test_invalid_world_never_runs_the_body(self):
        self.world.begin_world_scope.return_value = json.dumps({"ok": False, "error_code": "stale_world"})
        body = Mock()
        with self.assertRaises(scope.WorldScopeError):
            with scope.world_scope("expired"):
                body()
        body.assert_not_called()
        self.world.end_world_scope.assert_not_called()

    def test_body_failure_still_closes_scope(self):
        with self.assertRaisesRegex(ValueError, "original"):
            with scope.world_scope("world-1"):
                raise ValueError("original")
        self.world.end_world_scope.assert_called_once()

    def test_cleanup_failure_fails_successful_body(self):
        self.world.end_world_scope.return_value = json.dumps({"ok": False, "error_code": "order_mismatch"})
        with self.assertRaisesRegex(scope.WorldScopeError, "order_mismatch"):
            with scope.world_scope("world-1"):
                pass

    def test_cleanup_failure_preserves_original_failure(self):
        self.world.end_world_scope.side_effect = RuntimeError("cleanup")
        with self.assertRaisesRegex(ValueError, "original") as caught:
            with scope.world_scope("world-1"):
                raise ValueError("original")
        self.assertIn("cleanup", str(caught.exception.__notes__))

    def test_missing_native_token_fails_before_body(self):
        self.world.begin_world_scope.return_value = json.dumps({"ok": True})
        with self.assertRaisesRegex(scope.WorldScopeError, "token"):
            with scope.world_scope("world-1"):
                self.fail("Body must not execute")

    def test_nested_contexts_close_in_reverse_order(self):
        self.world.begin_world_scope.side_effect = [
            json.dumps({"ok": True, "scope_token": token}) for token in ("outer", "inner")
        ]
        with scope.world_scope("world-1"):
            with scope.world_scope("world-2"):
                pass
        self.assertEqual([call.kwargs["scope_token"] for call in self.world.end_world_scope.call_args_list], ["inner", "outer"])


if __name__ == "__main__":
    unittest.main()
