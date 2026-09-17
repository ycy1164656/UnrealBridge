"""Verify default-world and actor-name ambiguity guards in a live UE Editor.

Uses transient PIE settings and read-only gameplay/networking queries. No input,
asset editing, config saving, or content saving is performed. Run only after
the World identity service and both resolver changes have been loaded.
"""

from __future__ import annotations

import argparse
import json
import re
import time
from pathlib import Path
from typing import Any

from network_multiclient_v3_live_smoke import (
    _editor_state,
    _exec_json,
    _load_server,
    _native,
    _wait_for_pie,
)


def probe(server: Any, project: str, endpoint: str | None) -> dict[str, Any]:
    return _exec_json(
        server,
        project,
        "import json\n"
        "from unreal_bridge import Gameplay\n"
        "print(json.dumps({\n"
        " 'control_rotation_available': Gameplay.get_control_rotation() is not None,\n"
        " 'mapping_context_count': len(Gameplay.get_active_mapping_context_stack()),\n"
        "}))",
        endpoint,
    )


def sample_worlds(server: Any, project: str, endpoint: str | None) -> list[dict[str, Any]]:
    return _native(
        server, project, "Networking", "get_network_world_snapshots",
        {"replicated_actors_only": False, "max_actors_per_world": 256}, endpoint,
    )


def wait_for_worlds(
    server: Any, project: str, endpoint: str | None, count: int,
) -> list[dict[str, Any]]:
    initial_catalog = json.loads(_native(server, project, "World", "get_world_contexts", {}, endpoint))
    editor_worlds = [world for world in initial_catalog.get("worlds", []) if world["world_type"] == "Editor"]
    assert len(editor_worlds) == 1, "Expected one unambiguous editor world"
    expected_map = editor_worlds[0]["world_path"]
    deadline = time.monotonic() + 90.0
    previous_handles = None
    while time.monotonic() < deadline:
        worlds = sample_worlds(server, project, endpoint)
        catalog = json.loads(_native(server, project, "World", "get_world_contexts", {}, endpoint))
        pie = [world for world in catalog.get("worlds", []) if world["world_type"] == "PIE"]
        modes = sorted(world["net_mode"] for world in worlds)
        handles = tuple(sorted(world["world_handle"] for world in pie))
        ready = len(pie) == count and all(world["has_begun_play"] for world in pie)
        # Clients first expose /Temp/Untitled while connecting, then replace
        # that World during travel. Count/net mode alone is not readiness.
        ready = ready and all(re.sub(r"UEDPIE_\d+_", "", world["world"]) == expected_map for world in worlds)
        if len(worlds) == count and (
            count == 1 or modes == ["Client", "Client", "ListenServer"]
        ) and ready and handles == previous_handles:
            if count != 1 or probe(server, project, endpoint)["control_rotation_available"]:
                return worlds
        previous_handles = handles if ready else None
        time.sleep(0.5)  # Host process only; never sleep on the Unreal GameThread.
    raise TimeoutError(f"Expected {count} ready PIE worlds")


def run_session(
    server: Any, project: str, endpoint: str | None, count: int,
) -> dict[str, Any]:
    before = _editor_state(server, project, endpoint)
    assert not before["pie"], "Refusing to take over an existing PIE session"
    requested = False
    try:
        # Set before dispatch: a transport timeout can occur after UE accepts.
        requested = True
        assert _native(
            server, project, "Editor", "start_network_pie",
            {"client_count": count, "run_under_one_process": True}, endpoint,
        ) is True
        worlds = wait_for_worlds(server, project, endpoint, count)
        observation = probe(server, project, endpoint)
        assert observation["control_rotation_available"] is (count == 1), observation
        if count > 1:
            assert observation["mapping_context_count"] == 0, observation

        names_by_world = [
            {actor["actor_name"] for actor in world["actors"]}
            for world in worlds
        ]
        common = set.intersection(*names_by_world)
        assert common, "The bounded sample contains no common actor names"
        # Prefer a deterministic engine actor that exists on every peer.
        chosen = next((name for name in sorted(common) if "WorldSettings" in name), sorted(common)[0])
        paths = [
            next(actor["actor_path"] for actor in world["actors"] if actor["actor_name"] == chosen)
            for world in worlds
        ]
        assert len(set(paths)) == count, paths
        for actor_path in paths:
            info = _native(
                server, project, "Networking", "get_actor_networking_info",
                {"actor_name_or_label": actor_path}, endpoint,
            )
            assert info["actor_path"] == actor_path, {"expected": actor_path, "actual": info}
        if count > 1:
            ambiguous = _native(
                server, project, "Networking", "get_actor_networking_info",
                {"actor_name_or_label": chosen}, endpoint,
            )
            assert not ambiguous["actor_path"], ambiguous
        missing = _native(
            server, project, "Networking", "get_actor_networking_info",
            {"actor_name_or_label": paths[0] + "_Missing_UBGuard"}, endpoint,
        )
        assert not missing["actor_path"], missing
        return {
            "world_count": count,
            "worlds": [{k: w[k] for k in ("world", "net_mode", "pie_instance")} for w in worlds],
            "actor_name": chosen,
            "exact_paths": paths,
            "observation": observation,
            "ambiguous_name_rejected": count > 1,
            "missing_path_rejected": True,
        }
    finally:
        if requested:
            _native(server, project, "Editor", "stop_pie", {}, endpoint)
            _wait_for_pie(server, project, endpoint, False)
            assert _editor_state(server, project, endpoint) == before, "PIE/Dirty state drift"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", required=True)
    parser.add_argument("--endpoint")
    parser.add_argument("--report", required=True)
    args = parser.parse_args()
    report_path = Path(args.report).resolve()
    if report_path.exists():
        parser.error("Report already exists; choose a new path to preserve evidence")
    server = _load_server()
    ping = server.bridge_ping(project=args.project, endpoint=args.endpoint, timeout=15.0)
    assert ping.get("success") and ping.get("ready"), ping
    report: dict[str, Any] = {"result": "UB_WORLD_GUARD_FAIL", "project": args.project, "sessions": []}
    try:
        before = _editor_state(server, args.project, args.endpoint)
        assert not before["pie"] and not before["dirty_content"] and not before["dirty_maps"], before
        report["before"] = before
        assert not probe(server, args.project, args.endpoint)["control_rotation_available"]
        for count in (1, 3):
            report["sessions"].append(run_session(server, args.project, args.endpoint, count))
        report["after"] = _editor_state(server, args.project, args.endpoint)
        assert report["after"] == before
        report["result"] = "UB_WORLD_GUARD_PASS"
    except Exception as exc:
        report["error"] = f"{type(exc).__name__}: {exc}"
        raise
    finally:
        report_path.parent.mkdir(parents=True, exist_ok=True)
        with report_path.open("x", encoding="utf-8") as stream:
            json.dump(report, stream, ensure_ascii=False, indent=2)
        print(json.dumps({"result": report["result"], "report": str(report_path)}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
