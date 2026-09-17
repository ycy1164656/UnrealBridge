"""PhysicsAsset and mesh transactional acceptance against disposable copies.

The smoke duplicates read-only Engine/Lyra source meshes into unsaved temporary
packages, exercises typed official PhysicsAsset/static-mesh operations, captures
readbacks in the same ChangeSet, and requires complete new-package cleanup.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
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
ASSET_TOOLSET = "editor_toolset.toolsets.asset.AssetTools"
STATIC_MESH_TOOLSET = "editor_toolset.toolsets.static_mesh.StaticMeshTools"
PHYSICS_TOOLSET = "PhysicsToolsets.PhysicsAssetToolset"


def _load_server() -> Any:
    spec = importlib.util.spec_from_file_location("unreal_bridge_physics_mesh", SERVER_PATH)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _native(server: Any, endpoint: str, project: str, function: str) -> Any:
    response = server.bridge_call(
        "Editor", function, {}, endpoint=endpoint, project=project, timeout=60.0
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
    assert response.get("success") is True and payload, response
    return bool(payload["exists"])


def _call(toolset: str, tool: str, arguments: dict[str, Any]) -> dict[str, Any]:
    return {"toolset": toolset, "tool": tool, "arguments": arguments}


def _assert_new_asset_rollback(
    response: dict[str, Any], expected_objects: set[str]
) -> dict[str, Any]:
    assert response.get("success") is True, response
    official = response.get("official_result") or {}
    assert official.get("success") is True, response
    assert official.get("saved") is False, response
    assert official.get("side_effect_state") == "rolled-back", response
    change_set = official.get("change_set") or {}
    assert change_set.get("status") == "RolledBack", response
    assert change_set.get("rollback_verified") is True, response
    assert set(change_set.get("created_assets_for_job") or []) == expected_objects, response
    assert set(change_set.get("removed_created_assets_during_rollback") or []) == expected_objects, response
    return change_set


def run(project: str, endpoint: str) -> dict[str, Any]:
    server = _load_server()
    baseline_dirty = _native(server, endpoint, project, "get_dirty_package_names")

    skeletal_source = "/Game/Characters/Heroes/Mannequin/Meshes/SKM_Manny"
    skeletal_package = f"{FOLDER}/SK_PhysicsProbe"
    skeletal_object = f"{skeletal_package}.SK_PhysicsProbe"
    physics_package = f"{FOLDER}/SK_PhysicsProbe_PhysicsAsset"
    physics_object = f"{physics_package}.SK_PhysicsProbe_PhysicsAsset"
    for path in (skeletal_package, physics_package):
        assert not _exists(server, endpoint, project, path), path

    physics_response = server.bridge_call_official_transactional_batch(
        calls=[
            _call(
                ASSET_TOOLSET,
                "duplicate",
                {"path": skeletal_source, "new_path": skeletal_package},
            ),
            _call(
                PHYSICS_TOOLSET,
                "CreateFromMesh",
                {"meshPath": skeletal_package, "bAssignToMesh": False},
            ),
            _call(
                PHYSICS_TOOLSET,
                "GetBodyNames",
                {"physicsAsset": {"refPath": physics_object}},
            ),
            _call(
                PHYSICS_TOOLSET,
                "GetConstraints",
                {"physicsAsset": {"refPath": physics_object}},
            ),
        ],
        target_packages=[skeletal_package, physics_package],
        apply=False,
        allow_readbacks=True,
        endpoint=endpoint,
        project=project,
        timeout=240.0,
    )
    physics_change_set = _assert_new_asset_rollback(
        physics_response, {skeletal_object, physics_object}
    )
    for path in (skeletal_package, physics_package):
        assert not _exists(server, endpoint, project, path), physics_response

    static_package = f"{FOLDER}/SM_MeshProbe"
    static_object = f"{static_package}.SM_MeshProbe"
    assert not _exists(server, endpoint, project, static_package)
    mesh_response = server.bridge_call_official_transactional_batch(
        calls=[
            _call(
                ASSET_TOOLSET,
                "duplicate",
                {"path": "/Engine/BasicShapes/Cube", "new_path": static_package},
            ),
            _call(
                STATIC_MESH_TOOLSET,
                "set_material",
                {
                    "mesh": {"refPath": static_object},
                    "slot_name": "WorldGridMaterial",
                    "material": {
                        "refPath": "/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"
                    },
                },
            ),
            _call(
                STATIC_MESH_TOOLSET,
                "get_material",
                {
                    "mesh": {"refPath": static_object},
                    "slot_name": "WorldGridMaterial",
                },
            ),
            _call(
                STATIC_MESH_TOOLSET,
                "get_triangle_count",
                {"mesh": {"refPath": static_object}, "lod_index": 0},
            ),
        ],
        target_packages=[static_package],
        apply=False,
        allow_readbacks=True,
        endpoint=endpoint,
        project=project,
        timeout=180.0,
    )
    mesh_change_set = _assert_new_asset_rollback(mesh_response, {static_object})
    assert not _exists(server, endpoint, project, static_package), mesh_response

    bad_package = f"{FOLDER}/PA_InvalidProbe"
    bad_response = server.bridge_submit_physics_workflow(
        calls=[
            {
                "operation": f"epic:{PHYSICS_TOOLSET}.CreateFromMesh",
                "arguments": {"meshPath": "", "bAssignToMesh": False},
            }
        ],
        target_packages=[bad_package],
        apply=False,
        endpoint=endpoint,
        project=project,
        timeout=60.0,
    )
    assert bad_response.get("success") is False, bad_response
    assert not _exists(server, endpoint, project, bad_package)

    final_dirty = _native(server, endpoint, project, "get_dirty_package_names")
    assert final_dirty == baseline_dirty, {
        "baseline_dirty": baseline_dirty,
        "final_dirty": final_dirty,
    }
    return {
        "success": True,
        "physics": {
            "call_count": 4,
            "created_and_removed": physics_change_set["created_assets_for_job"],
            "body_and_constraint_readback": True,
            "rollback_verified": True,
        },
        "mesh": {
            "call_count": 4,
            "created_and_removed": mesh_change_set["created_assets_for_job"],
            "material_and_triangle_readback": True,
            "rollback_verified": True,
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
