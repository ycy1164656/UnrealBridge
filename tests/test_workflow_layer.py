from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
MODULE_PATH = (
    REPO
    / ".claude"
    / "skills"
    / "unreal-bridge"
    / "scripts"
    / "unreal_bridge_workflows.py"
)
spec = importlib.util.spec_from_file_location("unreal_bridge_workflows", MODULE_PATH)
workflow = importlib.util.module_from_spec(spec)
assert spec and spec.loader
sys.modules[spec.name] = workflow
spec.loader.exec_module(workflow)


class ResultShapingTests(unittest.TestCase):
    def test_terminal_job_json_comes_from_nested_result_output(self) -> None:
        response = {
            "output": "job-123",
            "job_result": {
                "output": "progress\n{\"success\": true, \"value\": 42}",
            },
        }
        self.assertEqual(
            workflow.last_json_object(response),
            {"success": True, "value": 42},
        )

    def test_projection_omit_and_cursor_are_stable(self) -> None:
        value = {
            "items": [
                {"id": index, "secret": f"secret-{index}"} for index in range(7)
            ],
            "metadata": {"owner": "bridge", "token": "hidden"},
        }
        projected = workflow.project_fields(
            value,
            fields=["items", "metadata.owner", "metadata.token"],
            omit=["metadata.token"],
        )
        first = workflow.paginate_value(projected, max_items=3, query_hash="query")
        second = workflow.paginate_value(
            projected,
            max_items=3,
            cursor=first["page"]["next_cursor"],
            query_hash="query",
        )

        self.assertEqual([item["id"] for item in first["value"]["items"]], [0, 1, 2])
        self.assertEqual([item["id"] for item in second["value"]["items"]], [3, 4, 5])
        self.assertEqual(projected["metadata"], {"owner": "bridge"})
        self.assertEqual(first["page"]["total"], 7)

    def test_projection_applies_to_each_top_level_list_item(self) -> None:
        projected = workflow.project_fields(
            [
                {"domain": "ai", "tool_count": 7, "internal": "drop"},
                {"domain": "ui", "tool_count": 12, "internal": "drop"},
            ],
            fields=["domain", "tool_count"],
        )
        self.assertEqual(
            projected,
            [
                {"domain": "ai", "tool_count": 7},
                {"domain": "ui", "tool_count": 12},
            ],
        )

    def test_cursor_cannot_be_replayed_for_another_query(self) -> None:
        first = workflow.paginate_value(
            [1, 2, 3], max_items=1, query_hash="first"
        )
        with self.assertRaisesRegex(ValueError, "does not belong"):
            workflow.paginate_value(
                [1, 2, 3],
                max_items=1,
                cursor=first["page"]["next_cursor"],
                query_hash="second",
            )

    def test_large_result_is_written_as_content_addressed_artifact(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = workflow.ArtifactStore(directory)
            value = {"items": [{"payload": "x" * 256} for _ in range(20)]}
            shaped = workflow.shape_result(
                value,
                max_items=2,
                artifact_store=store,
                artifact_threshold_bytes=1024,
            )
            artifact = shaped["artifact"]
            first = store.read(artifact["artifact_id"], max_bytes=128)
            self.assertEqual(shaped["page"]["returned"], 2)
            self.assertTrue(Path(artifact["path"]).is_file())
            self.assertIsNotNone(first["next_offset"])
            self.assertEqual(first["encoding"], "utf-8")

    def test_large_non_paginated_result_is_not_duplicated_inline(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = workflow.ArtifactStore(directory)
            value = {"transport": {"output": "x" * 4096}, "success": True}
            shaped = workflow.shape_result(
                value,
                artifact_store=store,
                artifact_threshold_bytes=1024,
            )
            self.assertTrue(shaped["result_truncated_to_artifact"])
            self.assertTrue(shaped["result"]["artifact_only"])
            self.assertEqual(
                shaped["result"]["summary"]["keys"],
                ["success", "transport"],
            )
            self.assertNotIn("x" * 100, json.dumps(shaped["result"]))
            artifact = store.read(
                shaped["artifact"]["artifact_id"], max_bytes=8192
            )
            restored = json.loads(artifact["content"])
            self.assertEqual(restored, value)


class ToolIndexTests(unittest.TestCase):
    def test_native_and_official_tools_share_one_search_index(self) -> None:
        manifest = {
            "libraries": {
                "UnrealBridgeUMGLibrary": {
                    "functions": {
                        "get_runtime_widget_tree": {
                            "tooltip": "Inspect active runtime widgets",
                            "risk": "ReadOnly",
                        }
                    }
                }
            }
        }
        catalog = {
            "toolsets": [
                {
                    "name": "SlateTools",
                    "module": "SlateToolset",
                    "tools": [
                        {
                            "name": "SlateTools.click",
                            "description": "Click a Slate widget",
                            "risk": "RuntimeInteraction",
                        }
                    ],
                }
            ]
        }
        index = workflow.ToolIndex(manifest, catalog)
        matches = index.search("widget")
        self.assertEqual({item["provider"] for item in matches}, {"UnrealBridge", "EpicToolsetRegistry"})
        self.assertEqual(index.describe(["epic:SlateTools.click"])[0]["function"], "click")
        self.assertEqual(sum(group["tool_count"] for group in index.domains()), 2)


class ScenarioTests(unittest.TestCase):
    @staticmethod
    def _wait(manager: object, scenario_id: str) -> dict:
        deadline = time.monotonic() + 5.0
        while time.monotonic() < deadline:
            state = manager.get(scenario_id)
            if state["status"] in {"succeeded", "failed", "cancelled"}:
                return state
            time.sleep(0.01)
        raise AssertionError("scenario did not reach a terminal state")

    def test_retry_assertion_persistence_and_success(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            attempts = {"count": 0}

            def execute(step: dict, timeout: float) -> dict:
                self.assertGreater(timeout, 0)
                attempts["count"] += 1
                if attempts["count"] == 1:
                    raise RuntimeError("transient")
                return {"value": 42}

            manager = workflow.ScenarioManager(directory, execute)
            submitted = manager.submit(
                {
                    "name": "retry",
                    "steps": [
                        {
                            "id": "read",
                            "risk": "ReadOnly",
                            "retry": 1,
                            "assertions": [
                                {"path": "value", "op": "eq", "value": 42}
                            ],
                        }
                    ],
                }
            )
            state = self._wait(manager, submitted["scenario_id"])
            self.assertEqual(state["status"], "succeeded")
            self.assertEqual(state["steps"][0]["attempts"], 2)
            self.assertTrue(state["steps"][0]["assertions"][0]["passed"])
            self.assertTrue(manager._path(state["scenario_id"]).is_file())

    def test_failure_rolls_back_completed_mutations_in_reverse_order(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            rolled_back: list[str] = []

            def execute(step: dict, timeout: float) -> dict:
                if step["id"] == "fail":
                    raise RuntimeError("expected failure")
                return {"change_set_id": f"cs-{step['id']}"}

            def rollback(step: dict, output: dict) -> dict:
                rolled_back.append(step["id"])
                return {"success": True, "change_set_id": output["change_set_id"]}

            manager = workflow.ScenarioManager(directory, execute, rollback)
            submitted = manager.submit(
                {
                    "steps": [
                        {"id": "one", "risk": "Mutating"},
                        {"id": "two", "risk": "Mutating"},
                        {"id": "fail", "risk": "ReadOnly"},
                    ]
                }
            )
            state = self._wait(manager, submitted["scenario_id"])
            self.assertEqual(state["status"], "failed")
            self.assertEqual(rolled_back, ["two", "one"])
            self.assertTrue(state["steps"][0]["rollback"]["success"])
            self.assertTrue(state["steps"][1]["rollback"]["success"])

    def test_mutating_step_assertion_failure_rolls_back_that_step(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            rolled_back: list[str] = []

            def execute(step: dict, timeout: float) -> dict:
                return {
                    "change_set_id": "cs-own-assertion",
                    "value": 1,
                    "rollback_required": True,
                }

            def rollback(step: dict, output: dict) -> dict:
                rolled_back.append(output["change_set_id"])
                return {"success": True}

            manager = workflow.ScenarioManager(directory, execute, rollback)
            submitted = manager.submit(
                {
                    "steps": [
                        {
                            "id": "mutate-and-check",
                            "risk": "Mutating",
                            "assertions": [
                                {"path": "value", "op": "eq", "value": 2}
                            ],
                        }
                    ]
                }
            )
            state = self._wait(manager, submitted["scenario_id"])
            self.assertEqual(state["status"], "failed")
            self.assertEqual(rolled_back, ["cs-own-assertion"])
            self.assertEqual(
                state["steps"][0]["output"]["change_set_id"],
                "cs-own-assertion",
            )
            self.assertFalse(state["steps"][0]["assertions"][0]["passed"])
            self.assertTrue(state["steps"][0]["rollback"]["success"])

    def test_mutating_retry_requires_idempotency_key(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            manager = workflow.ScenarioManager(directory, lambda step, timeout: {})
            submitted = manager.submit(
                {
                    "steps": [
                        {"id": "unsafe", "risk": "Mutating", "retry": 1}
                    ]
                }
            )
            state = self._wait(manager, submitted["scenario_id"])
            self.assertEqual(state["status"], "failed")
            self.assertIn("idempotency_key", state["error"])

    def test_persisted_state_survives_concurrent_polling(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            manager = workflow.ScenarioManager(
                directory,
                lambda step, timeout: {"step": step["id"]},
            )
            submitted = manager.submit(
                {
                    "steps": [
                        {"id": f"read-{index}", "risk": "ReadOnly"}
                        for index in range(20)
                    ]
                }
            )
            scenario_id = submitted["scenario_id"]
            errors: list[Exception] = []

            def poll() -> None:
                try:
                    while True:
                        state = manager.get(scenario_id)
                        if state["status"] in {"succeeded", "failed", "cancelled"}:
                            return
                except Exception as exc:  # pragma: no cover - failure evidence
                    errors.append(exc)

            readers = [threading.Thread(target=poll) for _ in range(4)]
            for reader in readers:
                reader.start()
            state = self._wait(manager, scenario_id)
            for reader in readers:
                reader.join(timeout=2.0)
            self.assertFalse(errors)
            self.assertEqual(state["status"], "succeeded")


if __name__ == "__main__":
    unittest.main(verbosity=2)
