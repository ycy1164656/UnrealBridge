"""Native Material/MetaSound intent preview and rollback acceptance.

The smoke creates only unsaved disposable assets, exercises the native typed
graph-intent APIs, proves that their synchronous ChangeSet finalization has
completed before restored-state readback, and then rolls back the disposable
asset creation itself.  No package is saved.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import time
import uuid
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
FOLDER = "/Game/__UnrealBridgeV3Smoke"


def _load_server() -> Any:
    spec = importlib.util.spec_from_file_location("unreal_bridge_native_graph", SERVER_PATH)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _native(
    server: Any,
    endpoint: str,
    project: str,
    library: str,
    function: str,
    **kwargs: Any,
) -> Any:
    response = server.bridge_call(
        library,
        function,
        kwargs,
        endpoint=endpoint,
        project=project,
        timeout=120.0,
    )
    payload = server._last_json_output(response)
    assert response.get("success") is True and payload and payload.get("ok"), response
    return payload.get("result")


def _exists(server: Any, endpoint: str, project: str, path: str) -> bool:
    response = server.bridge_exec(
        "import json, unreal\n"
        f"print(json.dumps({{'exists': bool(unreal.EditorAssetLibrary.does_asset_exist({path!r}))}}))",
        endpoint=endpoint,
        project=project,
        timeout=60.0,
        no_preflight=True,
    )
    payload = server._last_json_output(response)
    assert response.get("success") is True and isinstance(payload, dict), response
    return bool(payload["exists"])


def _wait_scenario(
    server: Any,
    endpoint: str,
    project: str,
    submitted: dict[str, Any],
    timeout: float = 900.0,
) -> dict[str, Any]:
    assert submitted.get("success") is True and submitted.get("scenario_id"), submitted
    deadline = time.monotonic() + timeout
    while True:
        response = server.bridge_get_scenario(
            submitted["scenario_id"], endpoint=endpoint, project=project
        )
        assert response.get("success") is True, response
        state = response["result"]
        if state["status"] in {"succeeded", "failed", "cancelled"}:
            assert state["status"] == "succeeded", state
            step = state["steps"][0]
            assert step["status"] == "succeeded", state
            output = step["output"]
            assert output.get("job_state") == "succeeded", output
            return output["official_result"]
        assert time.monotonic() < deadline, state
        time.sleep(0.1)


def run(project: str, endpoint: str) -> dict[str, Any]:
    server = _load_server()
    ping = server.bridge_ping(endpoint=endpoint, project=project, timeout=15.0)
    assert ping.get("success") is True and ping.get("plugin_version") == "3.0.0", ping
    baseline_dirty = _native(
        server, endpoint, project, "Editor", "get_dirty_package_names"
    )
    suffix = uuid.uuid4().hex[:8]

    material_package = f"{FOLDER}/M_NativeIntent_{suffix}"
    material_object = f"{material_package}.M_NativeIntent_{suffix}"
    assert not _exists(server, endpoint, project, material_package), material_package

    def material_exercise() -> dict[str, Any]:
        submitted = server.bridge_submit_material_graph_intent(
            material_path=material_object,
            ops=[
                {
                    "op": "add",
                    "class_name": "/Script/Engine.MaterialExpressionConstant",
                    "x": -240,
                    "y": 40,
                }
            ],
            create_if_missing=True,
            apply=False,
            compile=True,
            endpoint=endpoint,
            project=project,
        )
        result = _wait_scenario(server, endpoint, project, submitted)
        final = result.get("final_change_set") or {}
        assert result.get("success") is True and result.get("preview_rolled_back") is True, result
        assert result.get("ops_applied") == 1, result
        assert final.get("status") == "RolledBack", result
        assert final.get("rollback_verified") is True, result
        assert result.get("created_for_intent") is True, result
        assert final.get("created_assets_for_job"), result
        assert final.get("removed_created_assets_during_rollback"), result
        assert result.get("asset_exists_after_rollback") is False, result
        assert not _exists(server, endpoint, project, material_package), result
        return result

    material = material_exercise()

    metasound_package = f"{FOLDER}/MS_NativeIntent_{suffix}"
    metasound_object = f"{metasound_package}.MS_NativeIntent_{suffix}"
    probe_input = f"V3Probe_{suffix}"
    assert not _exists(server, endpoint, project, metasound_package), metasound_package

    def metasound_exercise() -> dict[str, Any]:
        submitted = server.bridge_submit_metasound_graph_intent(
            metasound_path=metasound_object,
            ops=[
                {
                    "op": "add_input",
                    "name": probe_input,
                    "data_type": "Float",
                    "literal_type": "Float",
                    "value": "0.5",
                }
            ],
            template_path=(
                "/Game/Audio/MetaSounds/"
                "sfx_Random_nl_meta.sfx_Random_nl_meta"
            ),
            apply=False,
            register_frontend=True,
            endpoint=endpoint,
            project=project,
        )
        result = _wait_scenario(server, endpoint, project, submitted)
        final = result.get("final_change_set") or {}
        assert result.get("success") is True and result.get("preview_rolled_back") is True, result
        assert result.get("ops_applied") == 1, result
        assert final.get("status") == "RolledBack", result
        assert final.get("rollback_verified") is True, result
        assert probe_input in (result.get("graph_diff") or {}).get("added_inputs", []), result
        assert result.get("created_for_intent") is True, result
        assert final.get("created_assets_for_job"), result
        assert final.get("removed_created_assets_during_rollback"), result
        assert result.get("asset_exists_after_rollback") is False, result
        assert not _exists(server, endpoint, project, metasound_package), result
        return result

    metasound = metasound_exercise()

    bad_meta = server.bridge_submit_metasound_graph_intent(
        metasound_path="/Game/DoesNotMatter.DoesNotMatter",
        ops=[{"op": "add_input", "name": "MissingType"}],
        apply=False,
        endpoint=endpoint,
        project=project,
    )
    assert bad_meta.get("success") is False and bad_meta.get("phase") == "metasound", bad_meta

    final_dirty = _native(
        server, endpoint, project, "Editor", "get_dirty_package_names"
    )
    assert final_dirty == baseline_dirty, {
        "baseline_dirty": baseline_dirty,
        "final_dirty": final_dirty,
    }
    return {
        "success": True,
        "plugin_version": ping["plugin_version"],
        "material": {
            "ops_applied": material["ops_applied"],
            "final_status": material["final_change_set"]["status"],
            "created_and_removed_in_one_change_set": True,
            "compile_checked": True,
        },
        "metasound": {
            "ops_applied": metasound["ops_applied"],
            "final_status": metasound["final_change_set"]["status"],
            "created_and_removed_in_one_change_set": True,
            "validation": metasound["validation_result"],
        },
        "invalid_input_structured_failure": True,
        "dirty_unchanged": True,
        "saved": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", required=True)
    parser.add_argument("--endpoint", required=True)
    args = parser.parse_args()
    print(json.dumps(run(args.project, args.endpoint), ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
