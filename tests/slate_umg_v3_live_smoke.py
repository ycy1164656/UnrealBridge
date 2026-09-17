"""Runtime UMG plus semantic Slate/Artifact/Golden acceptance on UE 5.8.

The test starts PIE only when needed, reads one live UMG widget through the
native semantic path/state API, and restores the original PIE state.  It then
runs SlateInspector exclusively through Unreal's Slate semantic operations;
no operating-system mouse or keyboard injection is used.  Screenshots and
large snapshots are retained only as Saved/UnrealBridge artifacts.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import time
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[1]
SERVER_PATH = (
    REPO_ROOT
    / ".claude"
    / "skills"
    / "unreal-bridge"
    / "scripts"
    / "unreal_bridge_mcp_server.py"
)


def _load_server() -> Any:
    spec = importlib.util.spec_from_file_location(
        "unreal_bridge_slate_umg_live", SERVER_PATH
    )
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _exec_json(
    server: Any,
    project: str,
    code: str,
    endpoint: str | None,
    timeout: float = 60.0,
) -> dict[str, Any]:
    response = server.bridge_exec(
        code,
        endpoint=endpoint,
        project=project,
        timeout=timeout,
        no_preflight=True,
    )
    payload = server._last_json_output(response)
    assert response.get("success") is True and isinstance(payload, dict), response
    return payload


def _editor_state(server: Any, project: str, endpoint: str | None) -> dict[str, Any]:
    return _exec_json(
        server,
        project,
        "import json, unreal\n"
        "print(json.dumps({\n"
        " 'pie': bool(unreal.EditorLevelLibrary.get_pie_worlds(False)),\n"
        " 'dirty_content': sorted(str(x.get_name()) for x in "
        "unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()),\n"
        " 'dirty_maps': sorted(str(x.get_name()) for x in "
        "unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()),\n"
        "}))",
        endpoint,
    )


def _wait_for_pie(
    server: Any,
    project: str,
    endpoint: str | None,
    expected: bool,
    timeout: float = 60.0,
) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if bool(_editor_state(server, project, endpoint)["pie"]) is expected:
            return
        time.sleep(0.5)
    raise TimeoutError(f"PIE did not reach expected state {expected}")


def _runtime_widget_probe(
    server: Any, project: str, endpoint: str | None
) -> dict[str, Any]:
    return _exec_json(
        server,
        project,
        "import json, unreal\n"
        "_lib = unreal.UnrealBridgeUMGLibrary\n"
        "_items = list(_lib.get_runtime_widget_tree('', '') or [])\n"
        "_candidate = next((x for x in _items if bool(x.found) and "
        "str(x.widget_object_path) and str(x.semantic_path) and str(x.slate_type)), None)\n"
        "if _candidate is None:\n"
        "    raise RuntimeError('No live UMG widget with a Slate instance was found')\n"
        "_state = _lib.get_runtime_widget_state(str(_candidate.widget_object_path))\n"
        "_missing_path = '/Engine/Transient.__UnrealBridgeV3MissingWidget'\n"
        "_missing = _lib.get_runtime_widget_state(_missing_path)\n"
        "print(json.dumps({\n"
        " 'runtime_widget_count': len(_items),\n"
        " 'object_path': str(_candidate.widget_object_path),\n"
        " 'semantic_path': str(_candidate.semantic_path),\n"
        " 'widget_class': str(_candidate.widget_class),\n"
        " 'world_type': str(_candidate.world_type),\n"
        " 'slate_type': str(_candidate.slate_type),\n"
        " 'state_found': bool(_state.found),\n"
        " 'state_object_path': str(_state.widget_object_path),\n"
        " 'state_semantic_path': str(_state.semantic_path),\n"
        " 'missing_found': bool(_missing.found),\n"
        " 'missing_error': str(_missing.error),\n"
        "}, ensure_ascii=False))",
        endpoint,
        timeout=120.0,
    )


def _wait_scenario(
    server: Any,
    project: str,
    scenario_id: str,
    endpoint: str | None,
    timeout: float = 240.0,
) -> dict[str, Any]:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        response = server.bridge_get_scenario(
            scenario_id, endpoint=endpoint, project=project
        )
        assert response.get("success") is True, response
        state = response["result"]
        if state["status"] in {"succeeded", "failed", "cancelled"}:
            return state
        time.sleep(0.25)
    raise TimeoutError(f"Scenario {scenario_id} did not finish")


def _step_map(state: dict[str, Any]) -> dict[str, dict[str, Any]]:
    return {str(step["id"]): step for step in state.get("steps", [])}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", default="ShooterRoyal")
    parser.add_argument("--endpoint")
    args = parser.parse_args()

    server = _load_server()
    ping = server.bridge_ping(
        project=args.project, endpoint=args.endpoint, timeout=15.0
    )
    assert ping.get("success") is True and ping.get("ready") is True, ping
    initial = _editor_state(server, args.project, args.endpoint)
    started_pie = not bool(initial["pie"])

    try:
        if started_pie:
            _exec_json(
                server,
                args.project,
                "import json, unreal\n"
                "unreal.EditorLevelLibrary.editor_play_simulate()\n"
                "print(json.dumps({'requested': True}))",
                args.endpoint,
            )
            _wait_for_pie(server, args.project, args.endpoint, True)

        runtime = _runtime_widget_probe(server, args.project, args.endpoint)
        assert runtime["runtime_widget_count"] > 0, runtime
        assert runtime["state_found"] is True, runtime
        assert runtime["state_object_path"] == runtime["object_path"], runtime
        assert runtime["state_semantic_path"], runtime
        assert runtime["missing_found"] is False, runtime
        assert "was not found" in runtime["missing_error"], runtime
    finally:
        if started_pie:
            _exec_json(
                server,
                args.project,
                "import json, unreal\n"
                "unreal.EditorLevelLibrary.editor_end_play()\n"
                "print(json.dumps({'requested': True}))",
                args.endpoint,
            )
            _wait_for_pie(server, args.project, args.endpoint, False)

    denied = server.bridge_submit_slate_workflow(
        [
            {
                "action": "WaitFor",
                "arguments": {
                    "text": "ShooterRoyal",
                    "textGone": "__UnrealBridgeV3DefinitelyAbsent__",
                },
            }
        ],
        allow_runtime_side_effects=False,
        capture_before=False,
        capture_after=False,
        endpoint=args.endpoint,
        project=args.project,
    )
    assert denied.get("success") is False, denied
    assert "allow_runtime_side_effects=true" in denied.get("error", ""), denied

    invalid = server.bridge_submit_slate_workflow(
        [{"action": "Hover", "arguments": {}}],
        allow_runtime_side_effects=True,
        capture_before=False,
        capture_after=False,
        endpoint=args.endpoint,
        project=args.project,
    )
    assert invalid.get("success") is False, invalid
    assert "actions[0]" in invalid.get("error", ""), invalid

    actions = [
        {
            "action": "WaitFor",
            "arguments": {
                "text": "ShooterRoyal",
                "textGone": "__UnrealBridgeV3DefinitelyAbsent__",
            },
        }
    ]
    submitted = server.bridge_submit_slate_workflow(
        actions,
        allow_runtime_side_effects=True,
        capture_before=True,
        capture_after=True,
        max_depth=30,
        endpoint=args.endpoint,
        project=args.project,
    )
    assert submitted.get("success") is True and submitted.get("scenario_id"), submitted
    assert submitted.get("os_input_injection") is False, submitted
    state = _wait_scenario(
        server, args.project, submitted["scenario_id"], args.endpoint
    )
    assert state["status"] == "succeeded", state
    steps = _step_map(state)
    expected_step_ids = {
        "before_snapshot",
        "before_screenshot",
        "action_0_WaitFor",
        "after_snapshot",
        "after_screenshot",
    }
    assert set(steps) == expected_step_ids, steps
    assert all(step["status"] == "succeeded" for step in steps.values()), steps
    assert steps["before_snapshot"]["output"].get("artifact"), steps
    assert steps["action_0_WaitFor"]["output"].get("artifact"), steps

    screenshot = steps["after_screenshot"]["output"]["image_artifact"]
    screenshot_path = Path(screenshot["path"])
    assert screenshot["media_type"] == "image/png", screenshot
    assert screenshot["size_bytes"] > 100 and screenshot_path.is_file(), screenshot
    artifact_chunk = server.bridge_read_artifact(
        screenshot["artifact_id"],
        offset=0,
        max_bytes=1024,
        endpoint=args.endpoint,
        project=args.project,
    )
    assert artifact_chunk.get("success") is True, artifact_chunk
    assert artifact_chunk.get("size_bytes") == screenshot["size_bytes"], artifact_chunk

    golden_submitted = server.bridge_submit_slate_workflow(
        actions,
        allow_runtime_side_effects=True,
        capture_before=False,
        capture_after=True,
        expected_path=str(screenshot_path),
        pixel_delta=16,
        max_mae=0.02,
        max_changed_ratio=0.05,
        fail_on_diff=True,
        endpoint=args.endpoint,
        project=args.project,
    )
    assert golden_submitted.get("success") is True, golden_submitted
    golden_state = _wait_scenario(
        server, args.project, golden_submitted["scenario_id"], args.endpoint
    )
    assert golden_state["status"] == "succeeded", golden_state
    golden_steps = _step_map(golden_state)
    integrated_golden = golden_steps["after_screenshot"]["output"]["golden"]
    assert integrated_golden.get("passed") is True, integrated_golden
    assert golden_steps["after_screenshot"]["output"].get(
        "golden_report_artifact"
    ), golden_steps

    exact_golden = server.bridge_compare_golden_image(
        str(screenshot_path),
        str(screenshot_path),
        pixel_delta=0,
        max_mae=0.0,
        max_changed_ratio=0.0,
        endpoint=args.endpoint,
        project=args.project,
    )
    assert exact_golden.get("success") is True, exact_golden
    assert exact_golden.get("passed") is True, exact_golden
    assert exact_golden.get("report_artifact"), exact_golden

    final = _editor_state(server, args.project, args.endpoint)
    assert final["pie"] == initial["pie"], {"initial": initial, "final": final}
    assert final["dirty_content"] == initial["dirty_content"], {
        "initial": initial,
        "final": final,
    }
    assert final["dirty_maps"] == initial["dirty_maps"], {
        "initial": initial,
        "final": final,
    }

    print(
        json.dumps(
            {
                "success": True,
                "plugin_version": ping.get("plugin_version"),
                "runtime_widget_count": runtime["runtime_widget_count"],
                "runtime_state_round_trip": True,
                "invalid_runtime_path_failure": True,
                "slate_wait_for": True,
                "snapshot_artifact": True,
                "screenshot_artifact_bytes": screenshot["size_bytes"],
                "artifact_chunk_read": True,
                "integrated_golden_passed": True,
                "exact_golden_passed": True,
                "runtime_opt_in_enforced": True,
                "schema_validation_enforced": True,
                "os_input_injection": False,
                "pie_restored": True,
                "dirty_unchanged": True,
                "content_saved": False,
            },
            ensure_ascii=False,
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
