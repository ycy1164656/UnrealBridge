"""Probe one exact ShooterRoyal PIE World for a frame-step projectile sample."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "skills/unreal-bridge/scripts"))
import unreal_bridge_mcp_server as bridge  # noqa: E402


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--endpoint", required=True)
    parser.add_argument("--project", required=True)
    parser.add_argument("--inspect-only", action="store_true")
    args = parser.parse_args()
    project = Path(args.project).resolve()
    if project.name != "ShooterRoyal.uproject":
        raise ValueError("ShooterRoyal project required")
    common = {"endpoint": args.endpoint, "project": str(project)}
    raw = bridge._last_json_output(bridge.bridge_call("World", "get_world_contexts", {"max_worlds": 16}, **common))
    catalog = json.loads(raw["result"])
    worlds = [world for world in catalog["worlds"] if world["world_type"] == "PIE"]
    servers = [world for world in worlds if world["net_mode"] == "ListenServer"]
    if len(worlds) != 3 or len(servers) != 1 or not catalog.get("pie_session_id"):
        raise RuntimeError("Exact three-world ListenServer session required")
    server_path = servers[0]["world_path"]
    code = (
        "import json, unreal\n"
        f"world = unreal.find_object(None, {server_path!r})\n"
        "cls = unreal.load_class(None, '/Script/ShooterRoyalRuntime.SRTowerProjectile')\n"
        "print(json.dumps({'world': bool(world), 'world_path': world.get_path_name() if world else None, "
        "'class': bool(cls), 'begin': hasattr(unreal.GameplayStatics, 'begin_deferred_actor_spawn_from_class'), "
        "'finish': hasattr(unreal.GameplayStatics, 'finish_spawning_actor'), "
        "'begin_doc': getattr(unreal.GameplayStatics.begin_deferred_actor_spawn_from_class, '__doc__', '')[:1400] "
        "if hasattr(unreal.GameplayStatics, 'begin_deferred_actor_spawn_from_class') else '', "
        "'world_spawn': [name for name in dir(world) if 'spawn' in name.lower()] if world else []}))"
    )
    submitted = bridge.bridge_submit_job(code, **common)
    if not submitted.get("success") or not submitted.get("job_id"):
        raise RuntimeError(f"inspect submit failed: {submitted}")
    result = bridge.bridge_wait_job(submitted["job_id"], wait_timeout=20, **common)
    print(json.dumps({"pie_session_id": catalog["pie_session_id"], "server_path": server_path,
                      "job": result}, ensure_ascii=False))


if __name__ == "__main__":
    main()
