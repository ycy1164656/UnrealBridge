import asyncio
import json
import sys
import tempfile
import time
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

SCRIPTS = Path(__file__).resolve().parents[1] / "skills/unreal-bridge/scripts"
sys.path.insert(0, str(SCRIPTS))

import unreal_bridge_mcp_server as server
from unreal_bridge_runtime_watch import RuntimeWatch, validate
from unreal_bridge_workflows import ArtifactStore


class Capability331Tests(unittest.TestCase):
    def test_full_surface_keeps_existing_tools(self):
        names = {tool.name for tool in asyncio.run(server.mcp.list_tools())}
        self.assertIn("bridge_call_domain_operation", names)
        self.assertIn("bridge_search_tools", names)
        self.assertIn("bridge_execute", names)

    def test_closed_workflow_registry_rejects_unknown_arguments(self):
        capability = server.HOST_CAPABILITIES.by_id["workflow:runtime_watch.start"]
        result = capability.call({"request": {}, "code": "print('arbitrary')"})
        self.assertFalse(result["success"])
        self.assertEqual(result["error_code"], "invalid_arguments")
        self.assertNotIn("workflow:python.execute", server.HOST_CAPABILITIES.by_id)
        prepare = server.HOST_CAPABILITIES.by_id["workflow:content_import.prepare"]
        bypass = prepare.call({"operation": "execute", "request_id": "x", "request": {}})
        self.assertEqual(bypass["error_code"], "invalid_arguments")

    def test_stale_schema_fails_before_native_dispatch(self):
        snapshot = SimpleNamespace(stale=False, fingerprint="test-fingerprint",
            tool_index=server.TOOL_INDEX, domain_registry=server.OFFICIAL_DOMAIN_REGISTRY,
            manifest=server.BRIDGE_MANIFEST, metadata=lambda: {})
        binding = SimpleNamespace(snapshot=snapshot, project=None, endpoint=None, token=None,
                                  selector_project=None, selector_endpoint=None)
        context = server._CATALOG_CONTEXT.set(binding)
        try:
            native = next(record for record in server._capability_records() if record["kind"] == "native")
            with patch.object(server, "bridge_call") as call:
                result = server.bridge_execute(native["id"], {}, "wrong-schema",
                    "test-fingerprint", server.HOST_CAPABILITIES.hash)
                self.assertEqual(result["error_code"], "stale_capability")
                call.assert_not_called()
        finally:
            server._CATALOG_CONTEXT.reset(context)

    def test_rejected_official_operation_stays_rejected(self):
        rejected = next(op for op in server.OFFICIAL_DOMAIN_REGISTRY.operations if op.access == "Rejected")
        snapshot = SimpleNamespace(stale=False, fingerprint="test-fingerprint",
            tool_index=server.TOOL_INDEX, domain_registry=server.OFFICIAL_DOMAIN_REGISTRY,
            manifest=server.BRIDGE_MANIFEST, metadata=lambda: {})
        binding = SimpleNamespace(snapshot=snapshot, project=None, endpoint=None, token=None,
                                  selector_project=None, selector_endpoint=None)
        context = server._CATALOG_CONTEXT.set(binding)
        try:
            capability_id = "official:" + rejected.id[len("epic:"):]
            with patch.object(server.OFFICIAL_DOMAIN_REGISTRY, "validate", return_value=[]):
                result = server.bridge_execute(capability_id, {}, rejected.schema_sha256,
                    snapshot.fingerprint, server.HOST_CAPABILITIES.hash)
            self.assertFalse(result["success"])
            self.assertEqual(result["phase"], "policy")
        finally:
            server._CATALOG_CONTEXT.reset(context)

    def test_watch_bounds_and_divergence_artifact(self):
        request = {"pie_session_id": "abc", "world_handles": ["ubr:world:a", "ubr:world:b"],
            "actor_name": "Actor", "class_path": "/Game/A.A_C",
            "fields": [{"kind": "property", "name": "Health"}],
            "duration_seconds": 0.15, "sample_hz": 20, "only_changes": True}
        with self.assertRaises(ValueError):
            validate({**request, "fields": [{"kind": "function", "name": "Delete"}]})
        with tempfile.TemporaryDirectory() as directory:
            store = ArtifactStore(directory)
            def sample(_request, sequence):
                value = sequence == 0
                return {"ok": True, "worlds": [
                    {"fields": [{"value": True}]},
                    {"fields": [{"value": value}]}]}
            watch = RuntimeWatch(request, sample, store)
            watch.start()
            watch.thread.join(timeout=3)
            view = watch.view()
            self.assertEqual(view["status"], "completed")
            self.assertEqual(view["observed_divergence_at_sample"], 1)
            self.assertTrue(Path(view["artifact"]["path"]).is_file())

    def test_watch_preserves_partial_timeline_on_snapshot_timeout(self):
        request = {"pie_session_id": "abc", "world_handles": ["ubr:world:a"],
            "actor_name": "Actor", "class_path": "/Game/A.A_C",
            "fields": [{"kind": "property", "name": "Health"}],
            "duration_seconds": 0.15, "sample_hz": 20, "only_changes": False}
        with tempfile.TemporaryDirectory() as directory:
            def sample(_request, sequence):
                if sequence == 1:
                    raise TimeoutError("snapshot_timeout")
                return {"ok": True, "worlds": [{"fields": [{"value": sequence}]}]}
            watch = RuntimeWatch(request, sample, ArtifactStore(directory))
            watch.start()
            watch.thread.join(timeout=3)
            view = watch.view()
            self.assertEqual(view["status"], "needs_reconciliation")
            saved = json.loads(Path(view["artifact"]["path"]).read_text(encoding="utf-8"))
            self.assertEqual(len(saved["timeline"]), 1)
            self.assertEqual(saved["timeline"][0]["sequence"], 0)

    def test_watch_short_request_still_samples_once(self):
        request = {"pie_session_id": "abc", "world_handles": ["ubr:world:a"],
            "actor_name": "Actor", "class_path": "/Game/A.A_C",
            "fields": [{"kind": "property", "name": "Health"}],
            "duration_seconds": 0.1, "sample_hz": 1}
        with tempfile.TemporaryDirectory() as directory:
            watch = RuntimeWatch(request, lambda _request, _sequence: {
                "ok": True, "worlds": [{"fields": [{"value": 1}]}]}, ArtifactStore(directory))
            watch.start()
            watch.thread.join(timeout=3)
            self.assertEqual(watch.view()["sample_count"], 1)


if __name__ == "__main__":
    unittest.main()
