"""Host job scoping: script fidelity and both execution slices."""
from __future__ import annotations

import sys
import types
import unittest
from contextlib import contextmanager
from unittest.mock import patch

from network_multiclient_v3_live_smoke import _load_server

server = _load_server()


class WorldJobScopeTests(unittest.TestCase):
    def test_original_python_and_handle_are_preserved(self):
        visits = []

        @contextmanager
        def scope(handle):
            visits.append(("enter", handle))
            try:
                yield
            finally:
                visits.append(("exit", handle))

        module = types.ModuleType("unreal_bridge_world_context")
        module.world_scope = scope
        handle = "world-'\\\n"
        original = "from __future__ import annotations\nvalue = '''a\nb'''\n# trailing comment"
        namespace = {}
        with patch.dict(sys.modules, {module.__name__: module}):
            exec(server._world_scoped_script(original, handle), namespace, namespace)
        self.assertEqual(namespace["value"], "a\nb")
        self.assertEqual(visits, [("enter", handle), ("exit", handle)])

    def test_start_and_poll_wrapped_after_original_preflight(self):
        with patch.object(server.bridge_cli, "_preflight_or_skip", return_value=([], [])) as preflight, \
             patch.object(server.bridge_cli, "_wrap_for_attr_enrichment", side_effect=lambda value: value), \
             patch.object(server, "_send_command", return_value={"success": True}) as send:
            server.bridge_submit_job("start = 1", poll_code="poll = 2", world_handle="world-1")
        preflight.assert_called_once_with("start = 1")
        payload = send.call_args.args[0]
        self.assertIn("_ub_world_scope('world-1')", payload["script"])
        self.assertIn("_ub_world_scope('world-1')", payload["poll_script"])
        self.assertIn("start = 1", payload["script"])
        self.assertIn("poll = 2", payload["poll_script"])

    def test_invalid_handle_never_dispatches(self):
        with patch.object(server, "_send_command") as send:
            for handle in ("", "x" * 257, 10):
                result = server.bridge_submit_job("pass", no_preflight=True, world_handle=handle)
                self.assertFalse(result["success"])
        send.assert_not_called()

    def test_no_scope_preserves_legacy_script(self):
        self.assertEqual(server._world_scoped_script("# comment\npass", None), "# comment\npass")


if __name__ == "__main__":
    unittest.main()
