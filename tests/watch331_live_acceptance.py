"""Run the bounded 10 Hz, three-world ShooterRoyal Watch acceptance case."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "skills/unreal-bridge/scripts"))
import unreal_bridge_mcp_server as bridge  # noqa: E402

FIELDS = ("MatchKills", "MatchDeaths", "MatchAssists", "bTeamRewardEligible",
          "LastCombatActivityServerTimeSeconds", "Score", "PlayerId",
          "CompressedPing", "bIsSpectator", "PlayerNamePrivate")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--endpoint", required=True)
    parser.add_argument("--project", required=True)
    args = parser.parse_args()
    if Path(args.project).resolve().name != "ShooterRoyal.uproject":
        raise ValueError("ShooterRoyal project required")
    common = {"endpoint": args.endpoint, "project": args.project}
    raw = bridge._last_json_output(bridge.bridge_call("World", "get_world_contexts", {"max_worlds": 16}, **common))
    catalog = json.loads(raw["result"])
    worlds = [item["world_handle"] for item in catalog["worlds"] if item["world_type"] == "PIE"]
    if len(worlds) != 3 or not catalog.get("pie_session_id"):
        raise RuntimeError("Exactly three PIE Worlds required")
    spec = {"pie_session_id": catalog["pie_session_id"], "world_handles": worlds,
            "actor_name": "SRPlayerState_1", "class_path": "/Script/ShooterRoyalRuntime.SRPlayerState",
            "fields": [{"kind": "property", "name": name} for name in FIELDS],
            "duration_seconds": 10, "sample_hz": 10, "only_changes": False}
    started = bridge.bridge_runtime_watch("start", request=spec, **common)
    if not started.get("success") or not started.get("watch_id"):
        raise RuntimeError(f"Watch start failed: {started}")
    watch = bridge._RUNTIME_WATCHES[started["watch_id"]]
    watch.thread.join(timeout=25)
    result = watch.view()
    print(json.dumps({"pie_session_id": catalog["pie_session_id"], "world_count": len(worlds),
                      "field_count": len(FIELDS), "result": result}, ensure_ascii=False))
    if not result["terminal"] or result["status"] != "completed":
        raise RuntimeError("Watch did not complete")


if __name__ == "__main__":
    main()
