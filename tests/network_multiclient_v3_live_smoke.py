"""Listen Server + two clients acceptance for UnrealBridge 3.0.

The session uses transient LevelEditor play settings, never saves config or
content, and always restores PIE state in ``finally``.
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
        "unreal_bridge_network_multiclient_live", SERVER_PATH
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


def _editor_state(
    server: Any, project: str, endpoint: str | None
) -> dict[str, Any]:
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
    timeout: float = 90.0,
) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if bool(_editor_state(server, project, endpoint)["pie"]) is expected:
            return
        time.sleep(0.5)
    raise TimeoutError(f"PIE did not reach expected state {expected}")


def _network_worlds(
    server: Any, project: str, endpoint: str | None
) -> list[dict[str, Any]]:
    result = _native(
        server,
        project,
        "UnrealBridgeNetworkingLibrary",
        "get_network_world_snapshots",
        {"replicated_actors_only": True, "max_actors_per_world": 256},
        endpoint,
    )
    assert isinstance(result, list), result
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", default="ShooterRoyal")
    parser.add_argument("--endpoint")
    args = parser.parse_args()

    server = _load_server()
    ping = server.bridge_ping(
        endpoint=args.endpoint, project=args.project, timeout=15.0
    )
    assert ping.get("success") is True and ping.get("ready") is True, ping

    before = _editor_state(server, args.project, args.endpoint)
    assert before["pie"] is False, before

    worlds: list[dict[str, Any]] = []
    try:
        requested = _native(
            server,
            args.project,
            "Editor",
            "start_network_pie",
            {"client_count": 3, "run_under_one_process": True},
            args.endpoint,
        )
        assert bool(requested) is True, requested
        _wait_for_pie(server, args.project, args.endpoint, True)

        deadline = time.monotonic() + 90.0
        while time.monotonic() < deadline:
            worlds = _network_worlds(server, args.project, args.endpoint)
            modes = sorted(str(world.get("net_mode")) for world in worlds)
            if len(worlds) == 3 and modes == ["Client", "Client", "ListenServer"]:
                listen_probe = next(
                    world for world in worlds if world["net_mode"] == "ListenServer"
                )
                client_probes = [
                    world for world in worlds if world["net_mode"] == "Client"
                ]
                connection_probe = {
                    actor["net_connection_path"]
                    for actor in listen_probe["actors"]
                    if actor.get("net_connection_path")
                }
                clients_ready = all(
                    world.get("net_driver_path")
                    and any(
                        actor.get("local_role") == "AutonomousProxy"
                        for actor in world["actors"]
                    )
                    for world in client_probes
                )
                if (
                    listen_probe.get("net_driver_path")
                    and len(connection_probe) >= 2
                    and clients_ready
                ):
                    break
            time.sleep(1.0)

        assert len(worlds) == 3, worlds
        assert sorted(world["net_mode"] for world in worlds) == [
            "Client",
            "Client",
            "ListenServer",
        ], worlds
        assert sorted(int(world["pie_instance"]) for world in worlds) == [0, 1, 2], worlds
        assert all(world["world_type"] == "PIE" for world in worlds), worlds
        assert all(world["net_driver_path"] for world in worlds), worlds
        assert all(world["total_actor_count"] > 0 for world in worlds), worlds
        assert all(world["total_replicated_actor_count"] > 0 for world in worlds), worlds
        assert all(isinstance(world["actors"], list) and world["actors"] for world in worlds), worlds

        listen = next(world for world in worlds if world["net_mode"] == "ListenServer")
        clients = [world for world in worlds if world["net_mode"] == "Client"]
        server_connections = {
            actor["net_connection_path"]
            for actor in listen["actors"]
            if actor.get("net_connection_path")
        }
        assert len(server_connections) >= 2, server_connections

        for client in clients:
            roles = {actor["local_role"] for actor in client["actors"]}
            assert "AutonomousProxy" in roles, client
            assert "SimulatedProxy" in roles, client

        common_classes = set.intersection(
            *({actor["class_path"] for actor in world["actors"]} for world in worlds)
        )
        assert "/Script/ShooterRoyalRuntime.SRGameState" in common_classes, common_classes

        class_audit = _native(
            server,
            args.project,
            "UnrealBridgeNetworkingLibrary",
            "audit_network_class",
            {
                "class_path": "/Script/LyraGame.LyraCharacter",
                "max_properties": 512,
                "max_rp_cs": 256,
            },
            args.endpoint,
        )
        assert class_audit["total_replicated_property_count"] > 0, class_audit
        assert class_audit["total_rpc_count"] > 0, class_audit
    finally:
        if _editor_state(server, args.project, args.endpoint)["pie"]:
            _native(server, args.project, "Editor", "stop_pie", {}, args.endpoint)
            _wait_for_pie(server, args.project, args.endpoint, False)

    after = _editor_state(server, args.project, args.endpoint)
    assert after == before, {"before": before, "after": after}

    summary = {
        "passed": True,
        "worlds": [
            {
                "pie_instance": world["pie_instance"],
                "net_mode": world["net_mode"],
                "replicated_actor_count": world["total_replicated_actor_count"],
            }
            for world in sorted(worlds, key=lambda item: int(item["pie_instance"]))
        ],
        "dirty_unchanged": True,
        "pie_restored": True,
    }
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
