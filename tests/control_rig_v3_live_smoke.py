"""Read/mutate/readback/rollback acceptance for an existing Control Rig asset.

The caller supplies a disposable existing asset.  The smoke never saves or
deletes it: it records the graph list, previews one add_graph operation through
the typed Control Rig workflow, verifies strict ChangeSet rollback, and then
requires the graph list and Dirty set to match the baseline exactly.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
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
    spec = importlib.util.spec_from_file_location("unreal_bridge_control_rig", SERVER_PATH)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _native(
    server: Any,
    endpoint: str,
    project: str,
    library: str,
    function: str,
) -> Any:
    response = server.bridge_call(
        library,
        function,
        {},
        endpoint=endpoint,
        project=project,
        timeout=60.0,
    )
    assert response.get("success") is True, response
    payload = server._last_json_output(response)
    assert payload and payload.get("ok") is True, response
    return payload.get("result")


def _asset_exists(server: Any, endpoint: str, project: str, asset_path: str) -> bool:
    response = server.bridge_exec(
        "import json\n"
        "import unreal\n"
        f"print(json.dumps({{'exists': bool(unreal.EditorAssetLibrary.does_asset_exist({asset_path!r}))}}))",
        endpoint=endpoint,
        project=project,
        timeout=60.0,
        no_preflight=True,
    )
    payload = server._last_json_output(response)
    assert response.get("success") is True and isinstance(payload, dict), response
    return bool(payload["exists"])


def _list_graphs(
    server: Any,
    endpoint: str,
    project: str,
    object_path: str,
) -> list[str]:
    submitted = server.bridge_submit_official_toolset_job(
        toolset=TOOLSET,
        tool="list_graphs",
        arguments={"control_rig": {"refPath": object_path}},
        endpoint=endpoint,
        project=project,
        queue_timeout=30.0,
        poll_interval=0.1,
        run_timeout=30.0,
        timeout=60.0,
    )
    assert submitted.get("success") is True and submitted.get("job_id"), submitted
    completed = server.bridge_wait_job(
        submitted["job_id"],
        wait_timeout=30.0,
        endpoint=endpoint,
        project=project,
    )
    assert completed.get("terminal") is True, completed
    assert completed.get("job_state") == "succeeded", completed
    payload = server._last_json_output(completed)
    assert payload and payload.get("success") is True, completed
    return [
        str(item["refPath"])
        for item in payload["result"]["returnValue"]
    ]


def run(project: str, endpoint: str, object_path: str) -> dict[str, Any]:
    server = _load_server()
    package_path = object_path.split(".", 1)[0]
    probe_name = "V3RollbackProbe"
    guard_package = "/Game/__UnrealBridgeV3Smoke/CR_V3PolicyGuard"

    ping = server.bridge_ping(endpoint=endpoint, project=project, timeout=15.0)
    assert ping.get("success") is True and ping.get("plugin_version") == "3.0.0", ping
    assert _asset_exists(server, endpoint, project, package_path)
    assert not _asset_exists(server, endpoint, project, guard_package)
    baseline_dirty = _native(
        server, endpoint, project, "Editor", "get_dirty_package_names"
    )
    baseline_graphs = _list_graphs(server, endpoint, project, object_path)
    assert not any(probe_name in graph for graph in baseline_graphs), baseline_graphs

    response = server.bridge_submit_control_rig_workflow(
        calls=[
            {
                "operation": f"epic:{TOOLSET}.add_graph",
                "arguments": {
                    "control_rig": {"refPath": object_path},
                    "name": probe_name,
                },
            },
            {
                "operation": f"epic:{TOOLSET}.list_graphs",
                "arguments": {"control_rig": {"refPath": object_path}},
            },
        ],
        target_packages=[package_path],
        apply=False,
        endpoint=endpoint,
        project=project,
        timeout=180.0,
    )
    assert response.get("success") is True, response
    official = response.get("official_result") or {}
    assert official.get("success") is True, response
    assert official.get("saved") is False, response
    assert official.get("side_effect_state") == "rolled-back", response
    change_set = official.get("change_set") or {}
    assert change_set.get("status") == "RolledBack", response
    assert change_set.get("rollback_verified") is True, response
    assert int(change_set.get("captured_objects_for_undo", 0)) > 0, response
    assert change_set.get("reload_packages_on_rollback") == [package_path], response
    assert change_set.get("reloaded_packages_during_rollback") == [package_path], response

    final_graphs = _list_graphs(server, endpoint, project, object_path)
    final_dirty = _native(
        server, endpoint, project, "Editor", "get_dirty_package_names"
    )
    assert final_graphs == baseline_graphs, {
        "baseline_graphs": baseline_graphs,
        "final_graphs": final_graphs,
        "response": response,
    }
    assert final_dirty == baseline_dirty, {
        "baseline_dirty": baseline_dirty,
        "final_dirty": final_dirty,
        "response": response,
    }

    guard = server.bridge_submit_control_rig_workflow(
        calls=[
            {
                "operation": f"epic:{TOOLSET}.create",
                "arguments": {"path": guard_package},
            }
        ],
        target_packages=[guard_package],
        apply=False,
        endpoint=endpoint,
        project=project,
        timeout=60.0,
    )
    assert guard.get("success") is False and guard.get("phase") == "policy", guard
    assert not _asset_exists(server, endpoint, project, guard_package)

    return {
        "success": True,
        "plugin_version": ping["plugin_version"],
        "asset": object_path,
        "baseline_graph_count": len(baseline_graphs),
        "preview_graph_observed": True,
        "captured_objects_for_undo": change_set["captured_objects_for_undo"],
        "reloaded_packages": change_set["reloaded_packages_during_rollback"],
        "rollback_verified": True,
        "graph_state_restored": True,
        "dirty_unchanged": True,
        "create_policy_guard": "Rejected before execution",
        "saved": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", required=True)
    parser.add_argument("--endpoint", required=True)
    parser.add_argument("--asset", required=True, help="Existing disposable Control Rig object path")
    args = parser.parse_args()
    print(
        json.dumps(
            run(args.project, args.endpoint, args.asset),
            ensure_ascii=False,
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
