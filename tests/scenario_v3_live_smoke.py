"""Live Scenario rollback acceptance using an existing disposable Control Rig."""

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
TOOLSET = "animation_toolset.toolsets.controlrig.ControlRigTools"


def _load_server() -> Any:
    spec = importlib.util.spec_from_file_location("unreal_bridge_scenario_live", SERVER_PATH)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _native(server: Any, endpoint: str, project: str, function: str) -> Any:
    response = server.bridge_call(
        "Editor",
        function,
        {},
        endpoint=endpoint,
        project=project,
        timeout=60.0,
    )
    payload = server._last_json_output(response)
    assert response.get("success") is True and payload and payload.get("ok"), response
    return payload.get("result")


def _list_graphs(server: Any, endpoint: str, project: str, asset: str) -> list[str]:
    submitted = server.bridge_submit_official_toolset_job(
        TOOLSET,
        "list_graphs",
        {"control_rig": {"refPath": asset}},
        endpoint=endpoint,
        project=project,
        queue_timeout=30.0,
        poll_interval=0.1,
        run_timeout=30.0,
        timeout=60.0,
    )
    assert submitted.get("success") is True, submitted
    completed = server.bridge_wait_job(
        submitted["job_id"],
        wait_timeout=30.0,
        endpoint=endpoint,
        project=project,
    )
    payload = server._last_json_output(completed)
    assert completed.get("job_state") == "succeeded" and payload, completed
    return [str(item["refPath"]) for item in payload["result"]["returnValue"]]


def run(project: str, endpoint: str, asset: str) -> dict[str, Any]:
    server = _load_server()
    package = asset.split(".", 1)[0]
    probe = "V3ScenarioRollbackProbe"
    baseline_graphs = _list_graphs(server, endpoint, project, asset)
    baseline_dirty = _native(server, endpoint, project, "get_dirty_package_names")
    assert not any(probe in graph for graph in baseline_graphs), baseline_graphs

    submitted = server.bridge_submit_scenario(
        {
            "name": "UnrealBridge 3.0 mutating assertion rollback acceptance",
            "steps": [
                {
                    "id": "mutate-own-assert-fails",
                    "type": "official_transactional_batch",
                    "calls": [
                        {
                            "toolset": TOOLSET,
                            "tool": "add_graph",
                            "arguments": {
                                "control_rig": {"refPath": asset},
                                "name": probe,
                            },
                        },
                        {
                            "toolset": TOOLSET,
                            "tool": "list_graphs",
                            "arguments": {"control_rig": {"refPath": asset}},
                        },
                    ],
                    "target_packages": [package],
                    "apply": True,
                    "allow_readbacks": True,
                    "artifact": True,
                    # Deliberately false: the step has already committed an
                    # unsaved ChangeSet when this assertion is evaluated.
                    "assertions": [
                        {"path": "success", "op": "eq", "value": False}
                    ],
                }
            ],
        },
        endpoint=endpoint,
        project=project,
    )
    assert submitted.get("success") is True, submitted
    deadline = time.monotonic() + 180.0
    while True:
        state_response = server.bridge_get_scenario(
            submitted["scenario_id"], endpoint=endpoint, project=project
        )
        assert state_response.get("success") is True, state_response
        state = state_response["result"]
        if state["status"] in {"succeeded", "failed", "cancelled"}:
            break
        assert time.monotonic() < deadline, state
        time.sleep(0.1)

    assert state["status"] == "failed", state
    step = state["steps"][0]
    assert step["status"] == "failed", step
    assert step["output"]["change_set_id"], step
    assert step["output"]["artifact"], step
    assert step["assertions"][0]["passed"] is False, step
    assert step["rollback"]["success"] is True, step
    assert step["rollback"]["status"] == "RolledBackAfterCommit", step

    final_graphs = _list_graphs(server, endpoint, project, asset)
    final_dirty = _native(server, endpoint, project, "get_dirty_package_names")
    assert final_graphs == baseline_graphs, {
        "baseline_graphs": baseline_graphs,
        "final_graphs": final_graphs,
        "state": state,
    }
    assert final_dirty == baseline_dirty, {
        "baseline_dirty": baseline_dirty,
        "final_dirty": final_dirty,
    }
    return {
        "success": True,
        "scenario_id": submitted["scenario_id"],
        "expected_terminal_status": state["status"],
        "own_assertion_failed": True,
        "rollback_status": step["rollback"]["status"],
        "artifact": step["output"]["artifact"],
        "graph_state_restored": True,
        "dirty_unchanged": True,
        "saved": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", required=True)
    parser.add_argument("--endpoint", required=True)
    parser.add_argument("--asset", required=True)
    args = parser.parse_args()
    print(json.dumps(run(args.project, args.endpoint, args.asset), ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
