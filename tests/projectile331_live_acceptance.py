"""Measure a ShooterRoyal projectile over exact owned PIE frame steps."""

from __future__ import annotations

import argparse
import json
import math
import sys
import time
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "skills/unreal-bridge/scripts"))
import unreal_bridge_mcp_server as bridge  # noqa: E402

OWNER_ID = "bridge331-projectile-acceptance"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--endpoint", required=True)
    parser.add_argument("--project", required=True)
    args = parser.parse_args()
    if Path(args.project).resolve().name != "ShooterRoyal.uproject":
        raise ValueError("ShooterRoyal project required")
    common = {"endpoint": args.endpoint, "project": args.project}
    run_id = "bridge331-projectile-step-" + uuid.uuid4().hex[:12]

    def call(library: str, function: str, arguments: dict) -> dict:
        observed = bridge._last_json_output(bridge.bridge_call(library, function, arguments, **common))
        if not isinstance(observed, dict) or not observed.get("ok"):
            raise RuntimeError(f"{library}.{function} failed: {observed}")
        return observed

    def catalog() -> dict:
        return json.loads(call("World", "get_world_contexts", {"max_worlds": 16})["result"])

    initial = catalog()
    if initial.get("pie_session_id") or len(initial.get("worlds", [])) != 1:
        raise RuntimeError("Clean Editor world with no PIE required")
    started = call("NetworkSession", "start_network_session", {"request_json": json.dumps({
        "schema": "unrealbridge.network_session.v2", "run_id": run_id,
        "editor_session_id": initial["editor_session_id"], "topology": "listen",
        "remote_client_count": 0, "out_lag_ms": 0, "out_loss_percent": 0,
    }, separators=(",", ":"))})
    state = json.loads(started["result"])
    pie_session = state.get("owned_pie_session_id", "")
    if not pie_session:
        raise RuntimeError(f"Native ownership was not established: {state}")
    controller_id = ""
    report: dict = {"schema": "unrealbridge.projectile331.live.v1", "run_id": run_id,
                    "pie_session_id": pie_session, "editor_session_id": initial["editor_session_id"]}
    try:
        for _ in range(40):
            state = json.loads(call("NetworkSession", "get_network_session_state", {"run_id": run_id})["result"])
            if state["status"] == "ready":
                break
            time.sleep(0.25)
        if state["status"] != "ready" or len(state.get("network_worlds", [])) != 1:
            raise RuntimeError(f"Single-world owned PIE not ready: {state}")
        report["topology"] = state
        begun = bridge.bridge_deterministic_pie("begin", owner_id=OWNER_ID, run_id=run_id,
                    pie_session_id=pie_session, fixed_delta_seconds=1 / 60, **common)
        if not begun.get("success") or begun.get("status") != "ready":
            raise RuntimeError(f"Frame controller begin failed: {begun}")
        controller_id = begun["controller_id"]
        spawned = call("Gameplay", "spawn_actor_in_pie", {
            "class_path": "/Script/ShooterRoyalRuntime.SRTowerProjectile",
            "location": {"x": 0, "y": 0, "z": 5000},
            "rotation": {"pitch": 0, "yaw": 0, "roll": 0},
        })["result"]
        if not isinstance(spawned, str) or not spawned:
            raise RuntimeError("Projectile did not spawn")
        report["actor_name"] = spawned

        def location() -> list[float]:
            value = call("Gameplay", "get_pie_actor_location", {"actor_name": spawned})["result"]
            point = value[0] if isinstance(value, list) and len(value) == 2 and value[1] else value
            if not isinstance(point, dict):
                raise RuntimeError(f"Projectile location unavailable: {value}")
            return [float(point[key]) for key in ("x", "y", "z")]

        baseline = location()
        one = bridge.bridge_deterministic_pie("step", owner_id=OWNER_ID, controller_id=controller_id,
                    frames=1, request_id="bridge331-projectile-frame-1", **common)
        if not one.get("success") or not one.get("paused_after"):
            raise RuntimeError(f"One frame was not exact: {one}")
        after_one = location()
        ten = bridge.bridge_deterministic_pie("step", owner_id=OWNER_ID, controller_id=controller_id,
                    frames=10, request_id="bridge331-projectile-frames-2-11", **common)
        if not ten.get("success") or not ten.get("paused_after"):
            raise RuntimeError(f"Ten frames were not exact: {ten}")
        after_eleven = location()
        delta_one = math.dist(baseline, after_one)
        delta_ten = math.dist(after_one, after_eleven)
        report.update({"baseline_cm": baseline, "after_one_frame_cm": after_one,
                       "after_eleven_frames_cm": after_eleven,
                       "distance_one_frame_cm": round(delta_one, 3),
                       "distance_next_ten_frames_cm": round(delta_ten, 3),
                       "step_one": one, "step_ten": ten})
        if not 10 <= delta_one <= 100 or not 100 <= delta_ten <= 1000:
            raise RuntimeError(f"Unexpected projectile motion: {delta_one}, {delta_ten}")
        print(json.dumps({"success": True, "pie_session_id": pie_session,
                          "actor_name": spawned, "distance_one_frame_cm": delta_one,
                          "distance_next_ten_frames_cm": delta_ten,
                          "ticks_one": list(one["world_tick_deltas"].values()),
                          "ticks_ten": list(ten["world_tick_deltas"].values())}, ensure_ascii=False))
    finally:
        if controller_id:
            report["controller_end"] = bridge.bridge_deterministic_pie("end", owner_id=OWNER_ID,
                                                    controller_id=controller_id, **common)
        stopped = call("NetworkSession", "stop_owned_network_session", {
            "run_id": run_id, "expected_pie_session": pie_session,
            "request_id": "bridge331-projectile-exact-stop"})
        report["stop_requested"] = json.loads(stopped["result"])
        for _ in range(40):
            final = json.loads(call("NetworkSession", "get_network_session_state", {"run_id": run_id})["result"])
            if final["status"] == "cleaned":
                break
            time.sleep(0.25)
        report["final_state"] = final
        output = ROOT / ".tmp" / ("projectile331-result-" + pie_session + ".json")
        output.parent.mkdir(exist_ok=True)
        output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        print(json.dumps({"evidence": str(output), "cleaned": final["status"] == "cleaned"}, ensure_ascii=False))


if __name__ == "__main__":
    main()
