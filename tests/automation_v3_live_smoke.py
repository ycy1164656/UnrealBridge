"""Run and validate the UnrealBridge UE Automation suite through MCP workflows."""

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
        "unreal_bridge_automation_mcp", SERVER_PATH
    )
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _official_return(step: dict[str, Any]) -> Any:
    output = step.get("output") or {}
    official = output.get("official_result") or {}
    result = official.get("result") or {}
    value = result.get("returnValue") if isinstance(result, dict) else result
    if isinstance(value, str):
        try:
            return json.loads(value)
        except json.JSONDecodeError:
            return value
    return value


def _failure_count(value: Any) -> int:
    """Conservatively count explicit failures in Automation result shapes."""
    failures = 0
    if isinstance(value, list):
        return sum(_failure_count(item) for item in value)
    if not isinstance(value, dict):
        return 0
    for key, item in value.items():
        normalized = str(key).lower().replace("_", "")
        if normalized in {"failed", "failurecount", "failedcount", "numfailed"}:
            if isinstance(item, bool):
                failures += int(item)
            elif isinstance(item, (int, float)):
                failures += int(item)
            elif isinstance(item, (list, dict, str)) and item:
                failures += len(item) if not isinstance(item, str) else 1
        elif normalized in {"state", "status", "result"} and isinstance(item, str):
            if item.lower() in {"failed", "fail", "error"}:
                failures += 1
        else:
            failures += _failure_count(item)
    return failures


def run(project: str, filter_expression: str) -> dict[str, Any]:
    server = _load_server()
    submitted = server.bridge_submit_automation_run(
        filter_expression=filter_expression,
        force_rediscover=True,
        project=project,
    )
    assert submitted.get("success") is True, submitted
    scenario_id = submitted["scenario_id"]
    deadline = time.monotonic() + 1800.0
    state: dict[str, Any] = {}
    while time.monotonic() < deadline:
        response = server.bridge_get_scenario(scenario_id, project=project)
        assert response.get("success") is True, response
        state = response["result"]
        if state["status"] in {"succeeded", "failed", "cancelled"}:
            break
        time.sleep(0.25)
    else:
        server.bridge_cancel_automation_run(scenario_id, project=project)
        raise AssertionError(f"Automation scenario timed out: {scenario_id}")

    assert state["status"] == "succeeded", {
        "scenario_id": scenario_id,
        "status": state["status"],
        "error": state.get("error"),
        "steps": [
            {"id": step["id"], "status": step["status"], "error": step.get("error")}
            for step in state["steps"]
        ],
    }
    steps = {step["id"]: step for step in state["steps"]}
    assert set(steps) == {"discover", "run", "status", "results"}
    assert all(step["status"] == "succeeded" for step in steps.values())

    discovered = _official_return(steps["discover"])
    run_result = _official_return(steps["run"])
    status = _official_return(steps["status"])
    results = _official_return(steps["results"])
    combined = {
        "discover": discovered,
        "run": run_result,
        "status": status,
        "results": results,
    }
    serialized = json.dumps(combined, ensure_ascii=False, default=str)
    assert filter_expression.lower() in serialized.lower(), combined
    failures = _failure_count(results)
    assert failures == 0, {"failure_count": failures, "results": results}

    return {
        "success": True,
        "scenario_id": scenario_id,
        "filter": filter_expression,
        "step_statuses": {key: value["status"] for key, value in steps.items()},
        "failure_count": failures,
        "result_summary": results,
        "state_path": submitted["state_path"],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", required=True)
    parser.add_argument("--filter", default="UnrealBridge")
    args = parser.parse_args()
    print(json.dumps(run(args.project, args.filter), ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
