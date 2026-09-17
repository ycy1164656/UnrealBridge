"""GameFeature, DataRegistry, AI, SmartObject, and networking acceptance.

All GameFeature runtime transitions are restored to their initial state.  PIE
is started only for bounded runtime diagnostics and is likewise restored.  No
asset package is saved or intentionally dirtied.
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
        "unreal_bridge_gameplay_systems_live", SERVER_PATH
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


def _native(
    server: Any,
    project: str,
    library: str,
    function: str,
    kwargs: dict[str, Any],
    endpoint: str | None,
    timeout: float = 90.0,
) -> Any:
    response = server.bridge_call(
        library,
        function,
        kwargs,
        endpoint=endpoint,
        project=project,
        timeout=timeout,
    )
    payload = server._last_json_output(response)
    assert response.get("success") is True and payload and payload.get("ok"), response
    return payload.get("result")


def _official(
    server: Any,
    project: str,
    domain: str,
    operation: str,
    arguments: dict[str, Any],
    endpoint: str | None,
    timeout: float = 90.0,
) -> dict[str, Any]:
    submitted = server.bridge_call_domain_operation(
        domain,
        operation,
        arguments,
        endpoint=endpoint,
        project=project,
        run_timeout=timeout,
        timeout=15.0,
    )
    assert submitted.get("success") is True and submitted.get("job_id"), submitted
    completed = server.bridge_wait_job(
        submitted["job_id"],
        wait_timeout=timeout,
        endpoint=endpoint,
        project=project,
    )
    assert completed.get("success") is True, completed
    assert completed.get("job_state") == "succeeded", completed
    job_result = completed.get("job_result") or {}
    payload = json.loads(job_result.get("output") or "{}")
    assert payload.get("success") is True and isinstance(payload.get("result"), dict), payload
    return payload["result"]


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


def _feature_info(
    server: Any, project: str, plugin: str, endpoint: str | None
) -> dict[str, Any]:
    result = _native(
        server,
        project,
        "UnrealBridgeGameFeatureLibrary",
        "get_game_feature_info",
        {"plugin_name_or_url": plugin, "run_data_validation": True},
        endpoint,
    )
    assert isinstance(result, dict), result
    return result


def _transition(
    server: Any,
    project: str,
    plugin: str,
    active: bool,
    endpoint: str | None,
) -> dict[str, Any]:
    submitted = server.bridge_submit_game_feature_transition(
        plugin,
        activate=active,
        allow_runtime_side_effects=True,
        run_data_validation=True,
        timeout=180.0,
        endpoint=endpoint,
        project=project,
    )
    assert submitted.get("success") is True and submitted.get("scenario_id"), submitted
    assert submitted.get("save_behavior") == "Never", submitted
    state = _wait_scenario(
        server, project, submitted["scenario_id"], endpoint, timeout=240.0
    )
    assert state["status"] == "succeeded", state
    steps = {step["id"]: step for step in state["steps"]}
    assert set(steps) == {
        "before_diagnostic",
        "request_transition",
        "wait_for_target_state",
    }, steps
    assert all(step["status"] == "succeeded" for step in steps.values()), steps
    wait_output = steps["wait_for_target_state"]["output"]
    assert wait_output["expected_active"] is active, wait_output
    assert wait_output["diagnostic"]["active"] is active, wait_output
    assert wait_output.get("artifact"), wait_output
    return state


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


def _runtime_diagnostics(
    server: Any, project: str, endpoint: str | None
) -> dict[str, Any]:
    behavior = _native(
        server,
        project,
        "UnrealBridgeAILibrary",
        "get_runtime_behavior_trees",
        {"max_components": 8, "max_blackboard_keys_per_component": 32},
        endpoint,
    )
    eqs = _native(
        server,
        project,
        "UnrealBridgeAILibrary",
        "get_runtime_eqs_queries",
        {"max_queries": 8, "max_items_per_query": 16},
        endpoint,
    )
    perception = _native(
        server,
        project,
        "UnrealBridgeAILibrary",
        "get_runtime_perception",
        {"max_components": 8, "max_stimuli_per_component": 32},
        endpoint,
    )
    smart_objects = _native(
        server,
        project,
        "UnrealBridgeSmartObjectLibrary",
        "get_runtime_smart_objects",
        {
            "max_components": 8,
            "max_slots_per_component": 16,
            "runtime_worlds_only": True,
        },
        endpoint,
    )
    network_worlds = _native(
        server,
        project,
        "UnrealBridgeNetworkingLibrary",
        "get_network_world_snapshots",
        {"replicated_actors_only": True, "max_actors_per_world": 128},
        endpoint,
    )
    class_audit = _native(
        server,
        project,
        "UnrealBridgeNetworkingLibrary",
        "audit_network_class",
        {
            "class_path": "/Script/LyraGame.LyraCharacter",
            "max_properties": 512,
            "max_rp_cs": 256,
        },
        endpoint,
    )
    invalid_class = _native(
        server,
        project,
        "UnrealBridgeNetworkingLibrary",
        "audit_network_class",
        {
            "class_path": "/Script/UnrealBridgeDefinitelyMissing.NoClass",
            "max_properties": 16,
            "max_rp_cs": 16,
        },
        endpoint,
    )
    assert isinstance(behavior, list), behavior
    assert isinstance(eqs, list), eqs
    assert isinstance(perception, list), perception
    assert isinstance(smart_objects, list), smart_objects
    assert isinstance(network_worlds, list), network_worlds
    assert isinstance(class_audit, dict), class_audit
    assert isinstance(invalid_class, dict), invalid_class
    return {
        "behavior": behavior,
        "eqs": eqs,
        "perception": perception,
        "smart_objects": smart_objects,
        "network_worlds": network_worlds,
        "class_audit": class_audit,
        "invalid_class": invalid_class,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", default="ShooterRoyal")
    parser.add_argument("--endpoint")
    parser.add_argument("--game-feature", default="ShooterRoyal")
    args = parser.parse_args()

    server = _load_server()
    ping = server.bridge_ping(
        endpoint=args.endpoint, project=args.project, timeout=15.0
    )
    assert ping.get("success") is True and ping.get("ready") is True, ping
    initial_editor = _editor_state(server, args.project, args.endpoint)
    assert initial_editor["pie"] is False, initial_editor

    feature_before = _feature_info(
        server, args.project, args.game_feature, args.endpoint
    )
    assert feature_before["plugin_name"] == args.game_feature, feature_before
    assert feature_before["descriptor_details_available"] is True, feature_before
    assert isinstance(feature_before["dependencies"], list), feature_before
    assert isinstance(feature_before["actions"], list), feature_before
    initial_active = bool(feature_before["active"])

    denied = server.bridge_submit_game_feature_transition(
        args.game_feature,
        activate=not initial_active,
        allow_runtime_side_effects=False,
        endpoint=args.endpoint,
        project=args.project,
    )
    assert denied.get("success") is False, denied
    assert "allow_runtime_side_effects=true" in denied.get("error", ""), denied

    transition_state: dict[str, Any] | None = None
    restore_state: dict[str, Any] | None = None
    try:
        transition_state = _transition(
            server,
            args.project,
            args.game_feature,
            not initial_active,
            args.endpoint,
        )
    finally:
        current_active = bool(
            _feature_info(server, args.project, args.game_feature, args.endpoint)[
                "active"
            ]
        )
        if current_active != initial_active:
            restore_state = _transition(
                server,
                args.project,
                args.game_feature,
                initial_active,
                args.endpoint,
            )
    assert transition_state is not None and restore_state is not None
    transition_key = transition_state["spec"]["steps"][1]["idempotency_key"]
    restore_key = restore_state["spec"]["steps"][1]["idempotency_key"]
    assert transition_key != restore_key, (transition_key, restore_key)
    assert bool(
        _feature_info(server, args.project, args.game_feature, args.endpoint)["active"]
    ) is initial_active

    registries_result = _official(
        server, args.project, "data_registry", "ListRegistries", {}, args.endpoint
    )
    registries = list(registries_result.get("returnValue") or [])
    assert registries, registries_result
    registry_name = str(registries[0])
    registry_info = _official(
        server,
        args.project,
        "data_registry",
        "GetRegistryInfo",
        {"registryName": registry_name},
        args.endpoint,
    )["returnValue"]
    registry_schema = _official(
        server,
        args.project,
        "data_registry",
        "GetSchema",
        {"registryName": registry_name},
        args.endpoint,
    )["returnValue"]
    data_sources = _official(
        server,
        args.project,
        "data_registry",
        "ListDataSources",
        {"registryName": registry_name},
        args.endpoint,
    )["returnValue"]
    runtime_sources = _official(
        server,
        args.project,
        "data_registry",
        "ListRuntimeSources",
        {"registryName": registry_name},
        args.endpoint,
    )["returnValue"]
    registry_items = _official(
        server,
        args.project,
        "data_registry",
        "ListItems",
        {"registryName": registry_name},
        args.endpoint,
    )["returnValue"]
    fetched_items = _official(
        server,
        args.project,
        "data_registry",
        "GetItems",
        {
            "registryName": registry_name,
            "itemNames": list(registry_items[:3]),
        },
        args.endpoint,
    )["returnValue"]
    assert registry_info["registryName"] == registry_name, registry_info
    assert isinstance(json.loads(registry_schema), dict), registry_schema
    assert isinstance(data_sources, list), data_sources
    assert isinstance(runtime_sources, list), runtime_sources
    assert isinstance(registry_items, list), registry_items
    assert isinstance(fetched_items, dict), fetched_items
    invalid_registry_call = server.bridge_call_domain_operation(
        "data_registry",
        "GetRegistryInfo",
        {},
        endpoint=args.endpoint,
        project=args.project,
    )
    assert invalid_registry_call.get("success") is False, invalid_registry_call
    assert invalid_registry_call.get("phase") == "schema", invalid_registry_call

    started_pie = True
    runtime: dict[str, Any] = {}
    try:
        _exec_json(
            server,
            args.project,
            "import json, unreal\n"
            "unreal.EditorLevelLibrary.editor_play_simulate()\n"
            "print(json.dumps({'requested': True}))",
            args.endpoint,
        )
        _wait_for_pie(server, args.project, args.endpoint, True)
        deadline = time.monotonic() + 60.0
        while True:
            runtime = _runtime_diagnostics(server, args.project, args.endpoint)
            if runtime["behavior"] and runtime["network_worlds"]:
                break
            if time.monotonic() >= deadline:
                raise TimeoutError("AI/network runtime diagnostics did not populate")
            time.sleep(1.0)

        behavior = runtime["behavior"]
        perception = runtime["perception"]
        network_worlds = runtime["network_worlds"]
        class_audit = runtime["class_audit"]
        assert 1 <= len(behavior) <= 8, behavior
        assert behavior[0]["tree_path"], behavior[0]
        assert isinstance(behavior[0]["blackboard_values"], list), behavior[0]
        assert 1 <= len(perception) <= 8, perception
        assert 1 <= len(network_worlds), network_worlds
        assert network_worlds[0]["total_actor_count"] > 0, network_worlds[0]
        assert network_worlds[0]["total_replicated_actor_count"] > 0, network_worlds[0]
        assert network_worlds[0]["actors"], network_worlds[0]
        assert class_audit["class_path"] == "/Script/LyraGame.LyraCharacter", class_audit
        assert class_audit["total_replicated_property_count"] > 0, class_audit
        assert class_audit["total_rpc_count"] > 0, class_audit
        assert runtime["invalid_class"]["class_path"] == "", runtime["invalid_class"]
        assert runtime["invalid_class"]["total_rpc_count"] == 0, runtime["invalid_class"]

        actor_path = str(network_worlds[0]["actors"][0]["actor_path"])
        actor_audit = _native(
            server,
            args.project,
            "UnrealBridgeNetworkingLibrary",
            "audit_network_actor",
            {
                "actor_name_or_path": actor_path,
                "max_properties": 512,
                "max_rp_cs": 256,
            },
            args.endpoint,
        )
        assert actor_audit["runtime"]["actor_path"] == actor_path, actor_audit
        assert actor_audit["class_audit"]["class_path"], actor_audit

        tree_ref = {"refPath": behavior[0]["tree_path"]}
        bt_nodes = _official(
            server,
            args.project,
            "ai",
            "list_nodes",
            {"behavior_tree": tree_ref},
            args.endpoint,
        )["returnValue"]
        bt_depths = _official(
            server,
            args.project,
            "ai",
            "get_node_depths",
            {"behavior_tree": tree_ref},
            args.endpoint,
        )["returnValue"]
        bt_blackboard = _official(
            server,
            args.project,
            "ai",
            "get_blackboard",
            {"behavior_tree": tree_ref},
            args.endpoint,
        )["returnValue"]
        assert isinstance(bt_nodes, list) and bt_nodes, bt_nodes
        assert isinstance(bt_depths, list) and len(bt_depths) == len(bt_nodes), (
            bt_nodes,
            bt_depths,
        )
        assert isinstance(bt_blackboard, dict) and bt_blackboard.get("refPath"), bt_blackboard
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

    final_editor = _editor_state(server, args.project, args.endpoint)
    assert final_editor == initial_editor, {
        "initial": initial_editor,
        "final": final_editor,
    }
    print(
        json.dumps(
            {
                "success": True,
                "plugin_version": ping.get("plugin_version"),
                "game_feature": args.game_feature,
                "game_feature_transition_and_restore": True,
                "game_feature_dependencies": len(feature_before["dependencies"]),
                "game_feature_actions": len(feature_before["actions"]),
                "transition_idempotency_scoped": True,
                "data_registries": len(registries),
                "data_registry": registry_name,
                "data_registry_sources": len(data_sources),
                "data_registry_runtime_sources": len(runtime_sources),
                "data_registry_items": len(registry_items),
                "behavior_trees": len(runtime["behavior"]),
                "eqs_queries": len(runtime["eqs"]),
                "perception_components": len(runtime["perception"]),
                "smart_objects": len(runtime["smart_objects"]),
                "network_worlds": len(runtime["network_worlds"]),
                "replicated_actors": runtime["network_worlds"][0][
                    "total_replicated_actor_count"
                ],
                "replicated_properties": runtime["class_audit"][
                    "total_replicated_property_count"
                ],
                "rpc_count": runtime["class_audit"]["total_rpc_count"],
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
