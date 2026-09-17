"""Preview/rollback acceptance for UnrealBridge 3.0 typed graph workflows.

The smoke deliberately creates uniquely named in-memory assets under a fixed
test folder, executes official UE 5.8 Toolset mutations with ``apply=False``,
and verifies that the Editor has neither an asset nor a new Dirty package after
each ChangeSet completes.  It never saves a package.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
from typing import Any, Callable


REPO_ROOT = Path(__file__).resolve().parents[1]
SERVER_PATH = (
    REPO_ROOT
    / ".claude"
    / "skills"
    / "unreal-bridge"
    / "scripts"
    / "unreal_bridge_mcp_server.py"
)
TEST_FOLDER = "/Game/__UnrealBridgeV3Smoke"


def _load_server() -> Any:
    spec = importlib.util.spec_from_file_location(
        "unreal_bridge_transactional_mcp", SERVER_PATH
    )
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _native(
    server: Any,
    project: str,
    library: str,
    function: str,
    endpoint: str | None = None,
    **kwargs: Any,
) -> Any:
    response = server.bridge_call(
        library,
        function,
        kwargs,
        endpoint=endpoint,
        project=project,
        timeout=60.0,
    )
    assert response.get("success") is True, response
    payload = server._last_json_output(response)
    assert payload and payload.get("ok") is True, response
    return payload.get("result")


def _asset_exists(
    server: Any,
    project: str,
    asset_path: str,
    endpoint: str | None = None,
) -> bool:
    response = server.bridge_exec(
        "import json\n"
        "import unreal\n"
        f"print(json.dumps({{'exists': bool(unreal.EditorAssetLibrary.does_asset_exist({asset_path!r}))}}))",
        endpoint=endpoint,
        project=project,
        timeout=60.0,
        no_preflight=True,
    )
    assert response.get("success") is True, response
    payload = server._last_json_output(response)
    assert isinstance(payload, dict) and "exists" in payload, response
    return bool(payload["exists"])


def _call(
    operation: str,
    arguments: dict[str, Any],
) -> dict[str, Any]:
    return {"operation": operation, "arguments": arguments}


def _as_bool(value: Any) -> bool:
    if isinstance(value, bool):
        return value
    return str(value).strip().lower() in {"true", "1"}


def _profiles(server: Any) -> dict[str, dict[str, Any]]:
    pcg_package = f"{TEST_FOLDER}/PCG_V3Preview"
    dataflow_package = f"{TEST_FOLDER}/DF_V3Preview"
    sequence_package = f"{TEST_FOLDER}/LS_V3Preview"
    material_package = f"{TEST_FOLDER}/M_V3Preview"
    pcg_object = f"{pcg_package}.PCG_V3Preview"
    dataflow_object = f"{dataflow_package}.DF_V3Preview"
    material_object = f"{material_package}.M_V3Preview"
    return {
        "pcg": {
            "asset": pcg_package,
            "submit": server.bridge_submit_pcg_workflow,
            "calls": [
                _call(
                    "epic:PCGToolset.PCGToolset.CreateGraph",
                    {"name": "PCG_V3Preview", "path": TEST_FOLDER},
                ),
                _call(
                    "epic:PCGToolset.PCGToolset.GetGraphStructure",
                    {"graph": {"refPath": pcg_object}},
                ),
            ],
        },
        "dataflow": {
            "asset": dataflow_package,
            "submit": server.bridge_submit_dataflow_workflow,
            "calls": [
                _call(
                    "epic:DataflowAgent.DataflowAgentToolset.CreateGraph",
                    {"name": "DF_V3Preview", "path": TEST_FOLDER},
                ),
                _call(
                    "epic:DataflowAgent.DataflowAgentToolset.GetGraphStructure",
                    {"graph": {"refPath": dataflow_object}},
                ),
            ],
        },
        "sequencer": {
            "asset": sequence_package,
            "submit": server.bridge_submit_sequencer_workflow,
            "calls": [
                _call(
                    "epic:animation_toolset.toolsets.sequencer.SequencerTools.create_level_sequence",
                    {
                        "package_path": TEST_FOLDER,
                        "asset_name": "LS_V3Preview",
                    },
                ),
                _call(
                    "epic:animation_toolset.toolsets.sequencer.SequencerTools.get_current_sequence",
                    {},
                ),
            ],
        },
        "material": {
            "asset": material_package,
            "submit": lambda **kwargs: server.bridge_submit_typed_domain_workflow(
                "material", **kwargs
            ),
            "calls": [
                _call(
                    "epic:editor_toolset.toolsets.material.MaterialTools.create_material",
                    {
                        "folder_path": TEST_FOLDER,
                        "asset_name": "M_V3Preview",
                    },
                ),
                _call(
                    "epic:editor_toolset.toolsets.material.MaterialTools.add_expression",
                    {
                        "material_or_function": {"refPath": material_object},
                        "expression_class": {
                            "refPath": "/Script/Engine.MaterialExpressionConstant"
                        },
                        "x": -200,
                        "y": 0,
                    },
                ),
                _call(
                    "epic:editor_toolset.toolsets.material.MaterialTools.get_expressions",
                    {"material_or_function": {"refPath": material_object}},
                ),
            ],
        },
    }


def _run_profile(
    server: Any,
    project: str,
    name: str,
    profile: dict[str, Any],
    baseline_dirty: Any,
    endpoint: str | None,
) -> dict[str, Any]:
    asset_path = str(profile["asset"])
    assert not _asset_exists(server, project, asset_path, endpoint), (
        f"Refusing to run: smoke asset already exists: {asset_path}"
    )
    submit: Callable[..., dict[str, Any]] = profile["submit"]
    response = submit(
        calls=profile["calls"],
        target_packages=[asset_path],
        apply=False,
        endpoint=endpoint,
        project=project,
        timeout=180.0,
    )
    assert response.get("success") is True, response
    official = response.get("official_result") or {}
    assert official.get("success") is True, response
    assert official.get("saved") is False, response
    assert official.get("side_effect_state") == "rolled-back", response
    change_set = official.get("change_set") or {}
    assert change_set.get("status") == "RolledBack", response
    assert change_set.get("rollback_verified") is True, response
    assert change_set.get("created_assets_for_job"), response
    assert change_set.get("removed_created_assets_during_rollback"), response
    assert not _asset_exists(server, project, asset_path, endpoint), response
    final_dirty = _native(
        server, project, "Editor", "get_dirty_package_names", endpoint=endpoint
    )
    assert final_dirty == baseline_dirty, {
        "profile": name,
        "baseline_dirty": baseline_dirty,
        "final_dirty": final_dirty,
        "response": response,
    }
    calls = official.get("calls") or []
    return {
        "asset": asset_path,
        "call_count": len(calls),
        "change_set_id": change_set.get("change_set_id"),
        "change_set_status": change_set.get("status"),
        "preview_dirty": (official.get("preview") or {}).get(
            "dirty_packages_for_job", []
        ),
        "created_assets": change_set.get("created_assets_for_job", []),
        "removed_assets": change_set.get(
            "removed_created_assets_during_rollback", []
        ),
        "rollback_verified": True,
        "asset_absent_after_rollback": True,
        "dirty_unchanged": True,
    }


def run(
    project: str,
    selected: list[str],
    endpoint: str | None = None,
) -> dict[str, Any]:
    server = _load_server()
    ping = server.bridge_ping(endpoint=endpoint, project=project, timeout=15.0)
    assert ping.get("success") is True and ping.get("plugin_version") == "3.0.0", ping
    baseline_dirty = _native(
        server, project, "Editor", "get_dirty_package_names", endpoint=endpoint
    )
    assert not _as_bool(
        _native(server, project, "Editor", "is_in_pie", endpoint=endpoint)
    )
    profiles = _profiles(server)
    unknown = sorted(set(selected) - set(profiles))
    assert not unknown, {"unknown_profiles": unknown, "valid": sorted(profiles)}

    results: dict[str, Any] = {}
    for name in selected:
        results[name] = _run_profile(
            server, project, name, profiles[name], baseline_dirty, endpoint
        )

    final_dirty = _native(
        server, project, "Editor", "get_dirty_package_names", endpoint=endpoint
    )
    assert final_dirty == baseline_dirty
    assert not _as_bool(
        _native(server, project, "Editor", "is_in_pie", endpoint=endpoint)
    )
    return {
        "success": True,
        "plugin_version": ping["plugin_version"],
        "save_behavior": "Never",
        "apply": False,
        "profiles": results,
        "dirty_unchanged": True,
        "final_pie": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", required=True)
    parser.add_argument("--endpoint")
    parser.add_argument(
        "--domains",
        nargs="+",
        default=["pcg", "dataflow", "sequencer", "material"],
    )
    args = parser.parse_args()
    print(
        json.dumps(
            run(args.project, args.domains, endpoint=args.endpoint),
            ensure_ascii=False,
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
