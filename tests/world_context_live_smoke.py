"""Real PIE world-scope, actor-identity and persistent-input acceptance.

Only transient sessions and zero-valued semantic input are used. Existing
assets are read, never edited or saved. The report path must be new.
"""
from __future__ import annotations

import argparse
import json
import time
from pathlib import Path
from typing import Any

from network_multiclient_v3_live_smoke import _editor_state, _exec_json, _load_server, _native, _wait_for_pie
from world_resolution_guard_live_smoke import wait_for_worlds


def native_json(server, project, function, arguments):
    value = _native(server, project, "World", function, arguments, None)
    return json.loads(value)


def wait_terminal(server, project, job_id):
    deadline = time.monotonic() + 45.0
    while time.monotonic() < deadline:
        state = server.bridge_get_job(job_id, project=project)
        assert state.get("success"), state
        if state.get("terminal"):
            return state
        time.sleep(0.1)
    raise TimeoutError(f"World job did not finish: {job_id}")


def scoped_job(server, project, handle, code, poll_code=None):
    result = server.bridge_submit_job(
        code=code, poll_code=poll_code, world_handle=handle,
        poll_interval=0.1, run_timeout=30.0, project=project,
    )
    assert result.get("success"), result
    state = wait_terminal(server, project, result["job_id"])
    assert state["job_state"] == "succeeded", state
    return server._last_json_output(state["job_result"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", required=True)
    parser.add_argument("--input-action", required=True)
    parser.add_argument("--report", required=True)
    args = parser.parse_args()
    destination = Path(args.report).resolve()
    if destination.exists():
        parser.error("Report exists; use a new output path")
    server = _load_server()
    ping = server.bridge_ping(project=args.project)
    assert ping.get("success") and ping.get("ready"), ping
    before = _editor_state(server, args.project, None)
    assert not before["pie"] and not before["dirty_content"] and not before["dirty_maps"], before
    report: dict[str, Any] = {"result": "UB_WORLD_CONTEXT_FAIL", "before": before, "assertions": []}
    started = False
    refs = []
    worlds = []
    try:
        started = True
        assert _native(server, args.project, "Editor", "start_network_pie", {"client_count": 3, "run_under_one_process": True}, None)
        snapshots = wait_for_worlds(server, args.project, None, 3)
        catalog = native_json(server, args.project, "get_world_contexts", {})
        assert catalog["ok"] and not catalog["truncated"], catalog
        worlds = [world for world in catalog["worlds"] if world["world_type"] == "PIE"]
        assert len(worlds) == 3, worlds
        assert len({world["world_handle"] for world in worlds}) == 3
        report["worlds"] = worlds
        report["editor_session_id"] = catalog["editor_session_id"]
        for world in worlds:
            snapshot = next(item for item in snapshots if item["world"] == world["world_path"])
            actor = next(item for item in snapshot["actors"] if "WorldSettings" in item["class_path"])
            ref = native_json(server, args.project, "resolve_actor_reference", {
                "world_handle": world["world_handle"], "actor_name_or_path": actor["actor_name"],
            })
            assert ref["ok"] and ref["actor_path"] == actor["actor_path"], ref
            refs.append(ref)
            body = (
                "import json\nfrom unreal_bridge import Gameplay, Networking\n"
                f"info=Networking.get_actor_networking_info(actor_name_or_label={ref['actor_handle']!r})\n"
                "print(json.dumps({'path': info.actor_path, 'controller': Gameplay.get_control_rotation() is not None}))"
            )
            result = scoped_job(server, args.project, world["world_handle"], body)
            assert result == {"path": actor["actor_path"], "controller": True}, result
        report["assertions"].append("all_three_worlds_resolve_and_observe_exactly")
        result = scoped_job(server, args.project, worlds[0]["world_handle"],
            "import json\nfrom unreal_bridge import World\n"
            f"print(World.validate_actor_reference(actor_handle={refs[1]['actor_handle']!r}))")
        assert result["ok"] is False, result
        report["assertions"].append("cross_world_actor_rejected")

        # Intentionally omit Python cleanup: the next job must not inherit it.
        _exec_json(server, args.project,
            "import json\nfrom unreal_bridge import World\n"
            f"print(World.begin_world_scope(world_handle={worlds[0]['world_handle']!r}))", None)
        result = _exec_json(server, args.project,
            "import json\nfrom unreal_bridge import Gameplay\n"
            "print(json.dumps({'controller': Gameplay.get_control_rotation() is not None}))", None)
        assert result["controller"] is False, result
        report["assertions"].append("unclosed_scope_does_not_leak")

        # Both the start and polling script must see the requested world.
        result = scoped_job(server, args.project, worlds[0]["world_handle"],
            "from unreal_bridge import Gameplay\nassert Gameplay.get_control_rotation() is not None\nprint('{}')",
            "import json\nfrom unreal_bridge import Gameplay\n"
            "print(json.dumps({'complete': True, 'controller': Gameplay.get_control_rotation() is not None}))")
        assert result["complete"] and result["controller"], result
        report["assertions"].append("poll_slice_reenters_scope")

        failing = server.bridge_submit_job(code="raise ValueError('owned scope failure probe')",
            world_handle=worlds[0]["world_handle"], project=args.project)
        assert failing["success"], failing
        assert wait_terminal(server, args.project, failing["job_id"])["job_state"] == "failed"
        polling = server.bridge_submit_job(code="print('{}')",
            poll_code="print('{\"complete\": false}')", poll_interval=0.1,
            world_handle=worlds[0]["world_handle"], project=args.project)
        assert polling["success"], polling
        deadline = time.monotonic() + 15.0
        while time.monotonic() < deadline:
            state = server.bridge_get_job(polling["job_id"], project=args.project)
            if state.get("job_state") == "running" and state.get("job_result", {}).get("phase") == "poll_wait":
                break
            time.sleep(0.1)
        else:
            raise TimeoutError("Scoped polling job did not reach poll_wait")
        assert server.bridge_cancel_job(polling["job_id"], project=args.project)["success"]
        assert wait_terminal(server, args.project, polling["job_id"])["job_state"] == "cancelled"
        after_failure = _exec_json(server, args.project,
            "import json\nfrom unreal_bridge import Gameplay\n"
            "print(json.dumps({'controller': Gameplay.get_control_rotation() is not None}))", None)
        assert not after_failure["controller"], after_failure
        report["assertions"].append("exception_and_poll_cancellation_leave_no_scope")

        client_worlds = [world for world in worlds if world["net_mode"] == "Client"]
        target, other = client_worlds
        result = scoped_job(server, args.project, target["world_handle"],
            "import json, unreal\nfrom unreal_bridge import Gameplay\n"
            f"print(json.dumps({{'set': Gameplay.set_sticky_input(input_action_path={args.input_action!r}, axis_value=unreal.Vector(0,0,0))}}))")
        assert result["set"], result
        time.sleep(0.4)  # Several real ticks; this sleep is outside the Editor.
        query = (
            "import json\nfrom unreal_bridge import Gameplay\n"
            "count, paths, values, remaining = Gameplay.dump_injected_input_queue()\n"
            "assert count == len(paths) == len(values) == len(remaining)\n"
            "print(json.dumps({'paths': list(paths)}))"
        )
        target_queue = scoped_job(server, args.project, target["world_handle"], query)
        other_queue = scoped_job(server, args.project, other["world_handle"], query)
        assert args.input_action in target_queue["paths"], target_queue
        assert args.input_action not in other_queue["paths"], other_queue
        scoped_job(server, args.project, other["world_handle"],
            "import json\nfrom unreal_bridge import Gameplay\nprint(json.dumps({'cleared': Gameplay.clear_sticky_input()}))")
        target_queue = scoped_job(server, args.project, target["world_handle"], query)
        assert args.input_action in target_queue["paths"], target_queue
        scoped_job(server, args.project, target["world_handle"],
            "import json\nfrom unreal_bridge import Gameplay\nprint(json.dumps({'cleared': Gameplay.clear_sticky_input()}))")
        report["assertions"].append("persistent_input_survives_job_and_clear_is_world_local")
        _native(server, args.project, "Editor", "stop_pie", {}, None)
        _wait_for_pie(server, args.project, None, False)
        started = False
        for ref in refs:
            assert not native_json(server, args.project, "validate_actor_reference", {"actor_handle": ref["actor_handle"]})["ok"]
        report["assertions"].append("all_old_pie_actor_handles_invalidated")
        started = True
        assert _native(server, args.project, "Editor", "start_network_pie", {"client_count": 1, "run_under_one_process": True}, None)
        wait_for_worlds(server, args.project, None, 1)
        for world, ref in zip(worlds, refs):
            invalid = native_json(server, args.project, "resolve_actor_reference", {
                "world_handle": world["world_handle"], "actor_name_or_path": ref["actor_path"]})
            assert not invalid["ok"] and invalid["error_code"] == "stale_world", invalid
            assert not native_json(server, args.project, "validate_actor_reference", {"actor_handle": ref["actor_handle"]})["ok"]
        _native(server, args.project, "Editor", "stop_pie", {}, None)
        _wait_for_pie(server, args.project, None, False)
        started = False
        report["assertions"].append("old_world_and_actor_identity_not_reused_by_new_pie")
        report["after"] = _editor_state(server, args.project, None)
        assert report["after"] == before, report["after"]
        report["result"] = "UB_WORLD_CONTEXT_PASS"
    except Exception as error:
        report["error"] = f"{type(error).__name__}: {error}"
        raise
    finally:
        if started:
            _native(server, args.project, "Editor", "stop_pie", {}, None)
            _wait_for_pie(server, args.project, None, False)
        report["final"] = _editor_state(server, args.project, None)
        destination.parent.mkdir(parents=True, exist_ok=True)
        with destination.open("x", encoding="utf-8") as stream:
            json.dump(report, stream, ensure_ascii=False, indent=2)
        print(json.dumps({"result": report["result"], "report": str(destination)}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
