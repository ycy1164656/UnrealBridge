"""Full UnrealBridge 3.0 Niagara transaction acceptance on UE 5.8.

The smoke clones a real Niagara system into an unsaved temporary package,
creates and renames typed User parameters, adds an emitter and module, binds
module inputs, creates and edits a nested Dynamic Input, compiles, reads the
mutated graph back, and rolls the whole ChangeSet back.  It also proves that an
invalid exact stack path fails structurally and rolls back.  No asset is saved.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import sys
import time
import uuid
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[1]
SCRIPT_DIR = REPO_ROOT / ".claude" / "skills" / "unreal-bridge" / "scripts"
SERVER_PATH = SCRIPT_DIR / "unreal_bridge_mcp_server.py"
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from unreal_bridge_niagara import (  # noqa: E402
    add_user_variables_call,
    dynamic_input_data,
    linked_input_data,
    literal_input_data,
    object_ref,
    rename_emitter_call,
    set_stack_input_call,
    stack_item_reference,
)


FOLDER = "/Game/__UnrealBridgeV3Smoke"
TEMPLATE_SYSTEM = (
    "/Game/Effects/Particles/Weapons/NS_WeaponFire.NS_WeaponFire"
)
TEMPLATE_EMITTER = (
    "/Game/Effects/Particles/Weapons/Emitters/"
    "NE_MuzzleFlashSmoke.NE_MuzzleFlashSmoke"
)
GRAVITY_MODULE = "/Niagara/Modules/Update/Forces/GravityForce.GravityForce"
RANDOM_RANGE_FLOAT = (
    "/Niagara/DynamicInputs/UniformRange/V2/"
    "RandomRangeFloat.RandomRangeFloat"
)


def _load_server() -> Any:
    spec = importlib.util.spec_from_file_location(
        "unreal_bridge_niagara_live", SERVER_PATH
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
        timeout=120.0,
    )
    payload = server._last_json_output(response)
    assert response.get("success") is True and payload and payload.get("ok"), response
    return payload.get("result")


def _asset_exists(
    server: Any,
    project: str,
    asset_path: str,
    endpoint: str | None,
) -> bool:
    response = server.bridge_exec(
        "import json, unreal\n"
        f"print(json.dumps({{'exists': bool(unreal.EditorAssetLibrary.does_asset_exist({asset_path!r}))}}))",
        endpoint=endpoint,
        project=project,
        timeout=60.0,
        no_preflight=True,
    )
    payload = server._last_json_output(response)
    assert response.get("success") is True and isinstance(payload, dict), response
    return bool(payload["exists"])


def _call(operation: str, arguments: dict[str, Any]) -> dict[str, Any]:
    return {"domain": "niagara", "operation": operation, "arguments": arguments}


def _assert_rolled_back(official: dict[str, Any]) -> dict[str, Any]:
    change_set = official.get("change_set") or {}
    assert official.get("saved", change_set.get("saved")) is False, official
    assert official.get("side_effect_state") == "rolled-back", official
    assert change_set.get("status") == "RolledBack", official
    assert change_set.get("rollback_verified") is True, official
    assert change_set.get("removed_created_assets_during_rollback"), official
    return change_set


def _first_difference(left: Any, right: Any, path: str = "$") -> str:
    if type(left) is not type(right):
        return f"{path}: type {type(left).__name__} != {type(right).__name__}"
    if isinstance(left, dict):
        left_keys = set(left)
        right_keys = set(right)
        if left_keys != right_keys:
            return f"{path}: keys {sorted(left_keys ^ right_keys)} differ"
        for key in sorted(left_keys):
            difference = _first_difference(left[key], right[key], f"{path}.{key}")
            if difference:
                return difference
        return ""
    if isinstance(left, list):
        if len(left) != len(right):
            return f"{path}: length {len(left)} != {len(right)}"
        for index, (left_item, right_item) in enumerate(zip(left, right)):
            difference = _first_difference(
                left_item, right_item, f"{path}[{index}]"
            )
            if difference:
                return difference
        return ""
    return "" if left == right else f"{path}: {left!r} != {right!r}"


def _module_ref(
    system: str,
    emitter: str,
    script: str,
    module: str,
    inputs: list[str] | None = None,
) -> dict[str, Any]:
    return stack_item_reference(
        system,
        emitter_name=emitter,
        script_name=script,
        module_name=module,
        input_name_stack=inputs,
    )


def _success_calls(system: str, asset_name: str) -> list[dict[str, Any]]:
    smoke_spawn = lambda inputs=None: _module_ref(  # noqa: E731
        system,
        "NE_MuzzleFlashSmoke",
        "ParticleSpawnScript",
        "InitializeParticle",
        inputs,
    )
    sparks_velocity = lambda inputs=None: _module_ref(  # noqa: E731
        system,
        "NE_MuzzleFlashSparks",
        "ParticleSpawnScript",
        "InheritVelocity",
        inputs,
    )

    calls: list[dict[str, Any]] = [
        _call(
            "CreateNiagaraSystem",
            {
                "assetName": asset_name,
                "assetPath": FOLDER,
                "templateSystem": object_ref(TEMPLATE_SYSTEM),
            },
        ),
        add_user_variables_call(
            system,
            [
                {
                    "name": "User.UB_Lifetime",
                    "type_ref_path": "/Script/Niagara.NiagaraFloat",
                    "default_value": {"value": 0.8},
                },
                {
                    "name": "User.UB_Color",
                    "type_ref_path": "/Script/CoreUObject.LinearColor",
                    "default_value": {"r": 1.0, "g": 0.25, "b": 0.05, "a": 1.0},
                },
                {
                    "name": "User.UB_Size",
                    "type_ref_path": "/Script/Niagara.NiagaraFloat",
                    "default_value": {"value": 96.0},
                },
                {
                    "name": "User.UB_Velocity",
                    "type_ref_path": "/Script/CoreUObject.Vector3f",
                    "default_value": {"x": 1.0, "y": 1.0, "z": 1.0},
                },
                {
                    "name": "User.UB_Enabled",
                    "type_ref_path": "/Script/Niagara.NiagaraBool",
                    "default_value": {"value": -1},
                },
            ],
        ),
        _call(
            "AddEmitter",
            {
                "system": object_ref(system),
                "templateEmitter": object_ref(TEMPLATE_EMITTER),
                "emitterName": "UB_ExtraEmitter",
            },
        ),
        _call(
            "AddModule",
            {
                "moduleLocationRef": _module_ref(
                    system, "UB_ExtraEmitter", "ParticleUpdateScript", ""
                ),
                "moduleAsset": object_ref(GRAVITY_MODULE),
            },
        ),
    ]

    bindings = [
        (smoke_spawn(["Lifetime Min"]), linked_input_data(
            "User.UB_Lifetime", "/Script/Niagara.NiagaraFloat"
        )),
        (smoke_spawn(["Color"]), linked_input_data(
            "User.UB_Color", "/Script/CoreUObject.LinearColor"
        )),
        (smoke_spawn(["Uniform Sprite Size Min"]), linked_input_data(
            "User.UB_Size", "/Script/Niagara.NiagaraFloat"
        )),
        (sparks_velocity(["Inherited Velocity Amount Scale"]), linked_input_data(
            "User.UB_Velocity", "/Script/CoreUObject.Vector3f"
        )),
    ]
    calls.extend(set_stack_input_call(ref, data) for ref, data in bindings)
    calls.extend(
        [
            set_stack_input_call(
                smoke_spawn(["Lifetime Max"]),
                dynamic_input_data(RANDOM_RANGE_FLOAT),
            ),
            set_stack_input_call(
                smoke_spawn(["Lifetime Max", "Minimum"]),
                literal_input_data(
                    "/Script/Niagara.NiagaraFloat", {"value": 0.6}
                ),
            ),
            set_stack_input_call(
                smoke_spawn(["Lifetime Max", "Maximum"]),
                literal_input_data(
                    "/Script/Niagara.NiagaraFloat", {"value": 1.4}
                ),
            ),
            rename_emitter_call(
                system, "NE_MuzzleFlashSmoke", "UB_RenamedSmoke"
            ),
        ]
    )

    renamed_spawn = lambda inputs=None: _module_ref(  # noqa: E731
        system,
        "UB_RenamedSmoke",
        "ParticleSpawnScript",
        "InitializeParticle",
        inputs,
    )
    calls.extend(
        [
            _call(
                "epic:NiagaraToolsets.NiagaraToolset_System.GetUserVariables",
                {"system": object_ref(system)},
            ),
            _call("GetSystemSummary", {"system": object_ref(system)}),
            _call(
                "GetModuleInputValues", {"moduleRef": renamed_spawn()}
            ),
            _call(
                "GetModuleInputValues", {"moduleRef": sparks_velocity()}
            ),
            _call(
                "GetDynamicInputChain",
                {"stackInputRef": renamed_spawn(["Lifetime Max"])},
            ),
            _call(
                "GetEmitterTopology",
                {
                    "emitterRef": _module_ref(
                        system, "UB_ExtraEmitter", "", ""
                    )
                },
            ),
        ]
    )
    return calls


def run(project: str, endpoint: str | None) -> dict[str, Any]:
    server = _load_server()
    ping = server.bridge_ping(project=project, endpoint=endpoint, timeout=15.0)
    assert ping.get("success") is True and ping.get("plugin_version") == "3.0.0", ping
    suffix = uuid.uuid4().hex[:10]
    asset_name = f"NS_NiagaraV3_{suffix}"
    package = f"{FOLDER}/{asset_name}"
    system = f"{package}.{asset_name}"
    assert not _asset_exists(server, project, package, endpoint), package

    baseline_dirty = _native(
        server, project, "Editor", "get_dirty_package_names", endpoint=endpoint
    )
    baseline_source = _native(
        server,
        project,
        "Niagara",
        "get_niagara_system_structure",
        endpoint=endpoint,
        system_path=TEMPLATE_SYSTEM,
    )

    response = server.bridge_call_official_transactional_batch(
        _success_calls(system, asset_name),
        target_packages=[package],
        apply=False,
        allow_readbacks=True,
        compile_niagara_system_path=system,
        niagara_user_parameter_renames=[
            {
                "old_name": "User.UB_Velocity",
                "new_name": "User.UB_VelocityRenamed",
            }
        ],
        endpoint=endpoint,
        project=project,
        timeout=600.0,
    )
    assert response.get("success") is True, response
    official = response.get("official_result") or {}
    compile_result = official.get("niagara_compile") or {}
    assert compile_result.get("success") is True, response
    assert compile_result.get("compile_complete") is True, response
    assert compile_result.get("ready_to_run") is True, response
    validation_result = official.get("niagara_validation") or {}
    assert validation_result.get("success") is True, response
    assert validation_result.get("error_count") == 0, response
    assert official.get("niagara_user_parameter_rename_count") == 1, response
    rename_results = official.get("niagara_user_parameter_renames") or []
    assert rename_results and rename_results[0].get("readback_verified") is True, response
    change_set = _assert_rolled_back(official)

    readbacks = [
        call.get("result")
        for call in official.get("calls") or []
        if call.get("access") == "ReadOnly"
    ]
    readback_text = json.dumps(readbacks, ensure_ascii=False, sort_keys=True)
    for expected in (
        "User.UB_Lifetime",
        "User.UB_Color",
        "User.UB_Size",
        "User.UB_Enabled",
        "User.UB_VelocityRenamed",
        "UB_RenamedSmoke",
        "UB_ExtraEmitter",
        "GravityForce",
        "RandomRangeFloat",
        "Minimum",
        "Maximum",
    ):
        assert expected in readback_text, {"missing": expected, "response": response}
    user_variable_calls = [
        call
        for call in official.get("calls") or []
        if call.get("tool") == "GetUserVariables"
    ]
    assert len(user_variable_calls) == 1, response
    user_variables = (
        (user_variable_calls[0].get("result") or {})
        .get("returnValue", {})
        .get("userVariables", [])
    )
    user_names = {item.get("name") for item in user_variables}
    assert "User.UB_Velocity" not in user_names, response
    assert "User.UB_VelocityRenamed" in user_names, response
    velocity_binding_calls = [
        call
        for call in official.get("calls") or []
        if call.get("tool") == "GetModuleInputValues"
        and "Inherited Velocity Amount Scale"
        in json.dumps(call.get("result"), ensure_ascii=False)
    ]
    assert len(velocity_binding_calls) == 1, response
    assert "User.UB_VelocityRenamed" in json.dumps(
        velocity_binding_calls[0].get("result"), ensure_ascii=False
    ), response

    assert not _asset_exists(server, project, package, endpoint), response
    final_dirty = _native(
        server, project, "Editor", "get_dirty_package_names", endpoint=endpoint
    )
    # Duplicating/compiling a system from a live template can briefly make the
    # template report NeedsCompile while Niagara finishes its asynchronous
    # dependency refresh.  Wait only while the durable graph structure is
    # identical; any real structural drift still fails immediately below.
    final_source: dict[str, Any] = {}
    deadline = time.monotonic() + 90.0
    while True:
        final_source = _native(
            server,
            project,
            "Niagara",
            "get_niagara_system_structure",
            endpoint=endpoint,
            system_path=TEMPLATE_SYSTEM,
        )
        if final_source == baseline_source:
            break
        baseline_graph = dict(baseline_source)
        final_graph = dict(final_source)
        for transient_key in ("needs_compile", "ready_to_run"):
            baseline_graph.pop(transient_key, None)
            final_graph.pop(transient_key, None)
        if final_graph != baseline_graph or time.monotonic() >= deadline:
            break
        time.sleep(0.5)
    assert final_dirty == baseline_dirty, {
        "baseline_dirty": baseline_dirty,
        "final_dirty": final_dirty,
    }
    assert final_source == baseline_source, (
        "Template Niagara system changed: "
        + _first_difference(baseline_source, final_source)
    )

    invalid_name = f"NS_NiagaraInvalid_{suffix}"
    invalid_package = f"{FOLDER}/{invalid_name}"
    invalid_system = f"{invalid_package}.{invalid_name}"
    invalid_ref = _module_ref(
        invalid_system,
        "NE_MuzzleFlashSmoke",
        "ParticleSpawnScript",
        "InitializeParticle",
        ["UB_Input_Does_Not_Exist"],
    )
    invalid_response = server.bridge_call_official_transactional_batch(
        [
            _call(
                "CreateNiagaraSystem",
                {
                    "assetName": invalid_name,
                    "assetPath": FOLDER,
                    "templateSystem": object_ref(TEMPLATE_SYSTEM),
                },
            ),
            set_stack_input_call(
                invalid_ref,
                literal_input_data(
                    "/Script/Niagara.NiagaraFloat", {"value": 1.0}
                ),
            ),
        ],
        target_packages=[invalid_package],
        apply=False,
        compile_niagara_system_path=invalid_system,
        endpoint=endpoint,
        project=project,
        timeout=300.0,
    )
    assert invalid_response.get("success") is False, invalid_response
    invalid_official = invalid_response.get("official_result") or {}
    assert invalid_official.get("error_code") == "OFFICIAL_TOOL_FAILED", invalid_response
    assert invalid_official.get("failed_call_index") == 1, invalid_response
    invalid_change_set = _assert_rolled_back(invalid_official)
    assert not _asset_exists(server, project, invalid_package, endpoint), invalid_response
    assert _native(
        server, project, "Editor", "get_dirty_package_names", endpoint=endpoint
    ) == baseline_dirty

    return {
        "success": True,
        "plugin_version": ping["plugin_version"],
        "asset": package,
        "mutation_count": response.get("mutation_count"),
        "readback_count": response.get("readback_count"),
        "user_parameters_created": 5,
        "user_parameters_renamed": 1,
        "emitter_added": True,
        "emitter_renamed": True,
        "module_added": "GravityForce",
        "bindings": 4,
        "dynamic_input": "RandomRangeFloat",
        "nested_inputs_set": ["Minimum", "Maximum"],
        "compile": compile_result,
        "validation": validation_result,
        "change_set_status": change_set.get("status"),
        "rollback_verified": change_set.get("rollback_verified"),
        "asset_absent_after_rollback": True,
        "dirty_unchanged": True,
        "template_unchanged": True,
        "invalid_path_error_code": invalid_official.get("error_code"),
        "invalid_path_failed_call_index": invalid_official.get("failed_call_index"),
        "invalid_path_rollback_verified": invalid_change_set.get(
            "rollback_verified"
        ),
        "saved": False,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", default="ShooterRoyal")
    parser.add_argument("--endpoint", default=None)
    args = parser.parse_args()
    print(json.dumps(run(args.project, args.endpoint), ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
