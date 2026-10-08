"""Exercise clean -> native warning -> repaired Map Check in an unsaved test map."""

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
    args = parser.parse_args()
    project = Path(args.project).resolve()
    if "bridge331-mapcheck-host" not in str(project).lower():
        raise ValueError("This smoke is restricted to the isolated Map Check host")
    common = {"endpoint": args.endpoint, "project": str(project)}

    def job(code: str) -> dict:
        submitted = bridge.bridge_submit_job(code, **common)
        if not submitted.get("success") or not submitted.get("job_id"):
            raise RuntimeError(f"job submit failed: {submitted}")
        completed = bridge.bridge_wait_job(submitted["job_id"], wait_timeout=20, **common)
        if completed.get("job_state") != "succeeded" or not completed.get("terminal"):
            raise RuntimeError(f"job incomplete: {completed}")
        return completed

    dirty = bridge._last_json_output(bridge.bridge_call("Editor", "get_dirty_package_names", {}, **common))
    worlds = bridge._last_json_output(bridge.bridge_call("World", "get_world_contexts", {"max_worlds": 8}, **common))
    catalog = json.loads(worlds["result"])
    if dirty.get("result") or catalog.get("pie_session_id") or len(catalog.get("worlds", [])) != 1:
        raise RuntimeError("Test host must start with one clean Editor world and no PIE")
    baseline = bridge.bridge_validation("map_check", **common)
    if not baseline.get("ok") or any(m["severity"] in ("warning", "error") for m in baseline["messages"]):
        raise RuntimeError(f"Map Check baseline is not clean: {baseline}")

    spawned = False
    negative = None
    repaired = None
    try:
        job("""import unreal
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actor = sub.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, 300), unreal.Rotator(0, 0, 0))
if not actor:
    raise RuntimeError('test actor spawn failed')
actor.set_actor_label('UBR331_MapCheck_NullMesh')
print(actor.get_path_name())""")
        spawned = True
        negative = bridge.bridge_validation("map_check", **common)
        if not negative.get("ok") or not any(
            m["severity"] in ("warning", "error") and "NULL StaticMesh" in m["message"]
            for m in negative["messages"]
        ):
            raise RuntimeError(f"Expected native null-mesh warning was absent: {negative}")
    finally:
        if spawned:
            job("""import unreal
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
matches = [a for a in sub.get_all_level_actors() if a.get_actor_label() == 'UBR331_MapCheck_NullMesh' and isinstance(a, unreal.StaticMeshActor)]
if len(matches) != 1 or not sub.destroy_actor(matches[0]):
    raise RuntimeError('exact test actor cleanup failed')
print('test_actor_removed')""")
            repaired = bridge.bridge_validation("map_check", **common)

    if not repaired or not repaired.get("ok") or any(
        m["severity"] in ("warning", "error") for m in repaired["messages"]
    ):
        raise RuntimeError(f"Map Check warning remained after repair: {repaired}")
    result = {"success": True, "schema": "unrealbridge.mapcheck331.live.v1",
              "project": str(project), "editor_session_id": catalog["editor_session_id"],
              "baseline": baseline, "negative": negative, "repaired": repaired,
              "saved_packages": []}
    destination = project.parent / "mapcheck331-result.json"
    destination.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps({"success": True, "evidence": str(destination),
                      "negative_warning_count": sum(m["severity"] == "warning" for m in negative["messages"]),
                      "repaired_warning_count": sum(m["severity"] == "warning" for m in repaired["messages"]),
                      "dirty_after": repaired.get("dirty_after")}, ensure_ascii=False))


if __name__ == "__main__":
    main()
