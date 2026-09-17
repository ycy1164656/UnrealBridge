"""Read-only/live-state acceptance smoke for the UnrealBridge 3.0 surface.

The script deliberately avoids package saves and content mutation.  It checks
native UE 5.8 adapters, exact official Toolset dispatch, persisted Scenarios,
runtime PIE introspection, and final Dirty/PIE restoration against a running
Editor.  Run inside the environment declared by ``mcp-requirements.txt``.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import re
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
    spec = importlib.util.spec_from_file_location("unreal_bridge_live_mcp", SERVER_PATH)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _native(server: Any, project: str, library: str, function: str, **kwargs: Any) -> Any:
    response = server.bridge_call(
        library,
        function,
        kwargs,
        project=project,
        timeout=60.0,
    )
    assert response.get("success") is True, response
    raw_response = response
    if response.get("result_truncated_to_artifact"):
        artifact_id = response["artifact"]["artifact_id"]
        offset = 0
        chunks: list[str] = []
        while True:
            chunk = server.bridge_read_artifact(
                artifact_id,
                offset=offset,
                max_bytes=1024 * 1024,
                project=project,
            )
            assert chunk.get("success") is True, chunk
            chunks.append(chunk["content"])
            next_offset = chunk.get("next_offset")
            if next_offset is None:
                break
            offset = int(next_offset)
        raw_response = json.loads("".join(chunks))
    elif response.get("call_success") is not None:
        raw_response = response["result"]
    payload = server._last_json_output(raw_response)
    assert payload and payload.get("ok") is True, response
    return payload.get("result")


def _as_bool(value: Any) -> bool:
    if isinstance(value, bool):
        return value
    return str(value).strip().lower() in {"true", "1"}


def _wait_for_pie(server: Any, project: str, expected: bool, timeout: float = 90.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if _as_bool(_native(server, project, "Editor", "is_in_pie")) is expected:
            return
        time.sleep(0.25)
    raise AssertionError(f"PIE did not reach expected state {expected}")


def _official_read(
    server: Any,
    project: str,
    domain: str,
    operation: str,
    arguments: dict[str, Any] | None = None,
) -> dict[str, Any]:
    submitted = server.bridge_call_domain_operation(
        domain,
        operation,
        arguments or {},
        project=project,
        run_timeout=180.0,
    )
    assert submitted.get("success") is True, submitted
    try:
        result = server._wait_scenario_job(
            submitted,
            deadline=time.monotonic() + 180.0,
            endpoint=None,
            project=project,
            token=None,
        )
    except (RuntimeError, TimeoutError) as exc:
        snapshot = server.bridge_get_job(
            str(submitted.get("job_id", "")), project=project
        )
        raise AssertionError(
            {
                "domain": domain,
                "operation": operation,
                "submitted": submitted,
                "snapshot": snapshot,
            }
        ) from exc
    assert result.get("job_state") == "succeeded", result
    assert result.get("official_result", {}).get("success") is not False, result
    return result


def _wait_scenario(server: Any, project: str, scenario_id: str) -> dict[str, Any]:
    deadline = time.monotonic() + 180.0
    while time.monotonic() < deadline:
        response = server.bridge_get_scenario(scenario_id, project=project)
        assert response.get("success") is True, response
        state = response["result"]
        if state["status"] in {"succeeded", "failed", "cancelled"}:
            return state
        time.sleep(0.1)
    raise AssertionError(f"scenario {scenario_id} did not reach a terminal state")


def run(project: str) -> dict[str, Any]:
    server = _load_server()
    ping = server.bridge_ping(project=project, timeout=15.0)
    assert ping.get("success") is True and ping.get("plugin_version") == "3.0.0", ping

    # Use both compact and fully-qualified library names.  Search results expose
    # the latter, while grouped MCP tools conventionally use the former.
    baseline_state = _native(
        server, project, "UnrealBridgeEditorLibrary", "get_editor_state"
    )
    baseline_dirty = _native(server, project, "Editor", "get_dirty_package_names")
    assert not _as_bool(_native(server, project, "Editor", "is_in_pie"))

    native_reads = [
        ("GameFeature", "list_game_features", {}),
        ("AI", "get_runtime_behavior_trees", {}),
        ("AI", "get_runtime_eqs_queries", {}),
        ("AI", "get_runtime_perception", {}),
        ("Networking", "get_network_world_snapshots", {}),
        ("StateTree", "get_runtime_state_trees", {}),
        ("WorldPartition", "get_world_partition_snapshots", {}),
        ("Audio", "get_runtime_audio_components", {}),
        ("SmartObject", "get_runtime_smart_objects", {}),
        ("UMG", "get_runtime_widget_tree", {}),
        ("PCG", "list_pcg_graph_assets", {"filter": ""}),
    ]
    native_results: dict[str, str] = {}
    for library, function, kwargs in native_reads:
        value = _native(server, project, library, function, **kwargs)
        native_results[f"{library}.{function}"] = type(value).__name__

    components = _native(
        server,
        project,
        "Blueprint",
        "get_blueprint_components",
        blueprint_path="/ShooterRoyal/Characters/B_Hero_SR_Default",
    )
    assert "local_scs" in str(components) and "local_cdo" in str(components), components
    resolved_component = _native(
        server,
        project,
        "Blueprint",
        "resolve_blueprint_component",
        blueprint_path="/ShooterRoyal/Characters/B_Hero_SR_Default",
        component_name="CharacterMesh0",
    )
    assert isinstance(resolved_component, dict), resolved_component
    assert str(resolved_component.get("source", "")).lower() in {
        "local_cdo",
        "ich_override",
        "parent_fallback",
    }, resolved_component

    official_calls = [
        ("game_feature", "ListDiscoveredGameFeaturePlugins", {}),
        ("data_registry", "ListRegistries", {}),
        ("niagara", "GetAssetDiscoveryInfo", {}),
        ("pcg", "ListNativeNodes", {}),
        ("dataflow", "ListNodeTypes", {}),
        ("gas", "FindAttributeSetClasses", {}),
        ("ui", "Windows", {}),
        ("ui", "Snapshot", {"ref": "", "maxDepth": 5, "bIncludeSourceLocations": False}),
        ("world", "get_current_level", {}),
    ]
    official_results: dict[str, str] = {}
    for domain, operation, arguments in official_calls:
        result = _official_read(server, project, domain, operation, arguments)
        official_results[f"{domain}.{operation}"] = str(result["job_id"])

    # Sequencer reads are intentionally context-sensitive.  Open an existing
    # Lyra sequence without editing it, verify the official read, then close the
    # asset editor so the smoke leaves the user's workspace state unchanged.
    sequence_path = "/ShooterMaps/LevelSequence/SEQ_ScreenCaptures"
    try:
        _native(server, project, "Editor", "open_asset", asset_path=sequence_path)
        time.sleep(1.0)
        sequence_result = _official_read(
            server, project, "sequencer", "get_current_sequence", {}
        )
        official_results["sequencer.get_current_sequence"] = str(
            sequence_result["job_id"]
        )
    finally:
        _native(
            server,
            project,
            "Editor",
            "close_asset_editor",
            asset_path=sequence_path,
        )

    submitted_scenario = server.bridge_submit_scenario(
        {
            "name": "UnrealBridge 3.0 live read-only acceptance",
            "steps": [
                {
                    "id": "editor-state",
                    "type": "call",
                    "library": "UnrealBridgeEditorLibrary",
                    "function": "get_editor_state",
                    "risk": "ReadOnly",
                    "retry": 1,
                    "assertions": [
                        {"path": "success", "op": "eq", "value": True},
                    ],
                    "artifact": True,
                },
                {
                    "id": "durable-job",
                    "type": "job",
                    "code": "print('unrealbridge-3.0-scenario-live-ok')",
                    "risk": "ReadOnly",
                    "retry": 1,
                    "run_timeout": 60.0,
                    "assertions": [
                        {"path": "job_state", "op": "eq", "value": "succeeded"},
                    ],
                    "artifact": True,
                },
            ],
        },
        project=project,
    )
    assert submitted_scenario.get("success") is True, submitted_scenario
    scenario = _wait_scenario(server, project, submitted_scenario["scenario_id"])
    assert scenario["status"] == "succeeded", scenario
    assert all(
        isinstance(step.get("output"), dict)
        and step["output"].get("artifact")
        for step in scenario["steps"]
    ), scenario

    runtime_results: dict[str, str] = {}
    try:
        _native(server, project, "Editor", "start_pie")
        _wait_for_pie(server, project, True)
        runtime_calls = [
            ("AI", "get_runtime_behavior_trees"),
            ("AI", "get_runtime_eqs_queries"),
            ("AI", "get_runtime_perception"),
            ("Networking", "get_network_world_snapshots"),
            ("StateTree", "get_runtime_state_trees"),
            ("WorldPartition", "get_world_partition_snapshots"),
            ("Audio", "get_runtime_audio_components"),
            ("SmartObject", "get_runtime_smart_objects"),
            ("UMG", "get_runtime_widget_tree"),
        ]
        for library, function in runtime_calls:
            value = _native(server, project, library, function)
            runtime_results[f"{library}.{function}"] = type(value).__name__
    finally:
        if _as_bool(_native(server, project, "Editor", "is_in_pie")):
            _native(server, project, "Editor", "stop_pie")
        _wait_for_pie(server, project, False)

    final_dirty = _native(server, project, "Editor", "get_dirty_package_names")
    assert final_dirty == baseline_dirty, {
        "baseline_dirty": baseline_dirty,
        "final_dirty": final_dirty,
    }
    assert not _as_bool(_native(server, project, "Editor", "is_in_pie"))

    return {
        "success": True,
        "plugin_version": ping["plugin_version"],
        "baseline_state": baseline_state,
        "dirty_unchanged": True,
        "native_reads": native_results,
        "official_read_jobs": official_results,
        "scenario": {
            "scenario_id": scenario["scenario_id"],
            "status": scenario["status"],
            "step_count": len(scenario["steps"]),
            "state_path": submitted_scenario["state_path"],
        },
        "runtime_pie_reads": runtime_results,
        "final_pie": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", required=True)
    args = parser.parse_args()
    print(json.dumps(run(args.project), ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
