"""Compare read-only snapshot round trips in the current owned three-world PIE."""

from __future__ import annotations

import argparse
import json
import statistics
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "skills/unreal-bridge/scripts"))
import unreal_bridge_mcp_server as bridge  # noqa: E402

FIELDS = ("MatchKills", "MatchDeaths", "MatchAssists", "bTeamRewardEligible",
          "LastCombatActivityServerTimeSeconds", "Score", "PlayerId",
          "CompressedPing", "bIsSpectator", "PlayerNamePrivate")


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    return round(ordered[min(len(ordered) - 1, int(len(ordered) * fraction))] * 1000, 2)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--endpoint", required=True)
    parser.add_argument("--project", required=True)
    parser.add_argument("--direct-count", type=int, default=20)
    parser.add_argument("--durable-count", type=int, default=10)
    args = parser.parse_args()
    common = {"endpoint": args.endpoint, "project": args.project}
    raw = bridge.bridge_call("World", "get_world_contexts", {"max_worlds": 32}, **common)
    catalog = json.loads(bridge._last_json_output(raw)["result"])
    worlds = [item["world_handle"] for item in catalog["worlds"] if item["world_type"] == "PIE"]
    if len(worlds) != 3 or not catalog.get("pie_session_id"):
        raise RuntimeError("Exactly three PIE Worlds required")
    spec = {"pie_session_id": catalog["pie_session_id"], "world_handles": worlds,
            "actor_name": "SRPlayerState_1", "class_path": "/Script/ShooterRoyalRuntime.SRPlayerState",
            "fields": [{"kind": "property", "name": name} for name in FIELDS]}
    encoded = json.dumps(spec, ensure_ascii=False)

    def direct() -> dict:
        result = bridge.bridge_call("RuntimeWatch", "get_multi_world_watch_snapshot",
                                    {"request_json": encoded}, no_preflight=True, timeout=5, **common)
        outer = bridge._last_json_output(result)
        if not isinstance(outer, dict) or not outer.get("ok"):
            raise RuntimeError(f"direct call failed: {result}")
        observed = json.loads(outer["result"])
        if not observed.get("ok") or len(observed.get("worlds", [])) != 3:
            raise RuntimeError(f"incomplete direct snapshot: {observed}")
        return observed

    def durable() -> dict:
        code = "import unreal\nprint(unreal.UnrealBridgeRuntimeWatchLibrary.get_multi_world_watch_snapshot(" + repr(encoded) + "))"
        submitted = bridge.bridge_submit_job(code, run_timeout=5, **common)
        if not submitted.get("success") or not submitted.get("job_id"):
            raise RuntimeError(f"submit failed: {submitted}")
        completed = bridge.bridge_wait_job(submitted["job_id"], wait_timeout=5, **common)
        observed = bridge._last_json_output(completed)
        if not isinstance(observed, dict) or not observed.get("ok") or len(observed.get("worlds", [])) != 3:
            raise RuntimeError(f"incomplete durable snapshot: {completed}")
        return observed

    report = {}
    for name, count, sample in (("direct", args.direct_count, direct),
                                ("durable", args.durable_count, durable)):
        values = []
        for _ in range(count):
            started = time.perf_counter()
            sample()
            values.append(time.perf_counter() - started)
        report[name] = {"count": count, "p50_ms": round(statistics.median(values) * 1000, 2),
                        "p95_ms": percentile(values, 0.95), "max_ms": round(max(values) * 1000, 2)}
    print(json.dumps(report, ensure_ascii=False))


if __name__ == "__main__":
    main()
