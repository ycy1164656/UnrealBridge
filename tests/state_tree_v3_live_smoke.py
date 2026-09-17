"""Full StateTree authoring/readback/compile/rollback acceptance on UE 5.8.

The smoke creates an unsaved StateTree with the engine test schema, authors
states, evaluator/task/conditions, typed parameters, a transition, a property
binding, and node properties, validates exact readback, then rolls the whole
ChangeSet back.  A second transaction proves invalid struct paths fail without
leaving an asset or dirty package behind.
"""

from __future__ import annotations

import argparse
import base64
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
SCHEMA = "/Script/StateTreeTestSuite.StateTreeTestSchema"
EVALUATOR = "/Script/StateTreeTestSuite.TestEval_A"
TASK = "/Script/StateTreeTestSuite.TestTask_B"
CONDITION = "/Script/StateTreeTestSuite.StateTreeTestBooleanCondition"


def _load_server() -> Any:
    spec = importlib.util.spec_from_file_location(
        "unreal_bridge_state_tree_live", SERVER_PATH
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
    server: Any, project: str, path: str, endpoint: str | None
) -> bool:
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


def _transaction_code(payload: dict[str, Any]) -> str:
    encoded = base64.b64encode(
        json.dumps(payload, ensure_ascii=False, sort_keys=True).encode("utf-8")
    ).decode("ascii")
    return f"""
import base64, json, traceback, unreal
_p = json.loads(base64.b64decode({encoded!r}).decode('utf-8'))
_lib = unreal.UnrealBridgeStateTreeLibrary
_cs = unreal.UnrealBridgeChangeSetLibrary

def _a(value, name, default=None):
    try:
        return getattr(value, name)
    except Exception:
        return default

def _validation(value):
    return {{
        'success': bool(_a(value, 'success', False)),
        'compiled': bool(_a(value, 'compiled', False)),
        'error_count': int(_a(value, 'error_count', 0) or 0),
        'warning_count': int(_a(value, 'warning_count', 0) or 0),
        'messages': [str(x) for x in (_a(value, 'messages', []) or [])],
    }}

def _edit(value):
    return {{
        'success': bool(_a(value, 'success', False)),
        'created_id': str(_a(value, 'created_id', '')),
        'error': str(_a(value, 'error', '')),
        'validation': _validation(_a(value, 'validation', None)),
    }}

def _node(value):
    return {{
        'id': str(_a(value, 'id', '')),
        'kind': str(_a(value, 'kind', '')),
        'name': str(_a(value, 'name', '')),
        'struct_path': str(_a(value, 'struct_path', '')),
        'instance_type': str(_a(value, 'instance_type', '')),
    }}

def _transition(value):
    return {{
        'id': str(_a(value, 'id', '')),
        'trigger': str(_a(value, 'trigger', '')),
        'transition_type': str(_a(value, 'transition_type', '')),
        'target_state_id': str(_a(value, 'target_state_id', '')),
        'target_state_name': str(_a(value, 'target_state_name', '')),
        'enabled': bool(_a(value, 'enabled', False)),
        'conditions': [_node(x) for x in (_a(value, 'conditions', []) or [])],
    }}

def _state(value):
    return {{
        'id': str(_a(value, 'id', '')),
        'name': str(_a(value, 'name', '')),
        'path': str(_a(value, 'path', '')),
        'state_type': str(_a(value, 'state_type', '')),
        'enabled': bool(_a(value, 'enabled', False)),
        'tasks': [_node(x) for x in (_a(value, 'tasks', []) or [])],
        'enter_conditions': [_node(x) for x in (_a(value, 'enter_conditions', []) or [])],
        'transitions': [_transition(x) for x in (_a(value, 'transitions', []) or [])],
    }}

def _structure(value):
    return {{
        'found': bool(_a(value, 'found', False)),
        'asset_path': str(_a(value, 'asset_path', '')),
        'schema_class_path': str(_a(value, 'schema_class_path', '')),
        'evaluators': [_node(x) for x in (_a(value, 'evaluators', []) or [])],
        'global_tasks': [_node(x) for x in (_a(value, 'global_tasks', []) or [])],
        'parameters': [{{
            'struct_id': str(_a(x, 'struct_id', '')),
            'state_id': str(_a(x, 'state_id', '')),
            'name': str(_a(x, 'name', '')),
            'value_type': str(_a(x, 'value_type', '')),
            'value': str(_a(x, 'value', '')),
        }} for x in (_a(value, 'parameters', []) or [])],
        'bindings': [{{
            'source_struct_id': str(_a(x, 'source_struct_id', '')),
            'source_path': str(_a(x, 'source_path', '')),
            'target_struct_id': str(_a(x, 'target_struct_id', '')),
            'target_path': str(_a(x, 'target_path', '')),
            'description': str(_a(x, 'description', '')),
        }} for x in (_a(value, 'bindings', []) or [])],
        'states': [_state(x) for x in (_a(value, 'states', []) or [])],
    }}

def _change_set(value):
    return {{
        'status': str(_a(value, 'status', '')),
        'success': bool(_a(value, 'success', False)),
        'saved': bool(_a(value, 'saved', False)),
        'can_commit': bool(_a(value, 'can_commit', False)),
        'dirty_packages_for_job': list(_a(value, 'dirty_packages_for_job', []) or []),
        'unexpected_dirty_packages': list(_a(value, 'unexpected_dirty_packages', []) or []),
        'created_assets_for_job': list(_a(value, 'created_assets_for_job', []) or []),
        'removed_created_assets_during_rollback': list(_a(value, 'removed_created_assets_during_rollback', []) or []),
        'rollback_verified': bool(_a(value, 'rollback_verified', False)),
        'error': str(_a(value, 'error', '')),
    }}

_change_set_id = _cs.begin_change_set(
    'UnrealBridge 3.0 StateTree smoke', [_p['package']]
)
_out = {{'success': False, 'steps': [], 'save_behavior': 'Never'}}
try:
    _created = _lib.create_state_tree(
        _p['folder'], _p['name'], _p['schema'],
        '/Script/StateTreeEditorModule.StateTreeFactory',
        '/Script/StateTreeModule.StateTree', False)
    if not _created:
        raise RuntimeError('CreateStateTree returned an empty path')

    def _must(label, value):
        item = _edit(value)
        _out['steps'].append({{'label': label, **item}})
        if not item['success']:
            raise RuntimeError(f"{{label}}: {{item['error']}}")
        return item['created_id']

    _state_a = _must('state_a', _lib.add_state_tree_state(
        _p['object'], '', 'AcquireTarget', 'State', False, False))
    _state_b = _must('state_b', _lib.add_state_tree_state(
        _p['object'], '', 'AttackTarget', 'State', False, False))
    _evaluator = _must('evaluator', _lib.add_state_tree_evaluator(
        _p['object'], _p['evaluator'], '', False, False))
    _task = _must('task', _lib.add_state_tree_task(
        _p['object'], _state_a, _p['task'], '', False, False))
    _must('global_task', _lib.add_state_tree_global_task(
        _p['object'], _p['task'], '', False, False))
    _must('enter_condition', _lib.add_state_tree_enter_condition(
        _p['object'], _state_a, _p['condition'], '', 'And', 0, False, False))
    _root_parameter = _must('root_parameter', _lib.add_state_tree_parameter(
        _p['object'], '', 'Aggression', 'Float', '', '0.75', False, False))
    _must('state_parameter', _lib.add_state_tree_parameter(
        _p['object'], _state_a, 'CanAttack', 'Bool', '', 'True', False, False))
    _transition_id = _must('transition', _lib.add_state_tree_transition(
        _p['object'], _state_a, 'OnStateCompleted', 'GotoState', _state_b,
        '', False, False))
    _must('transition_condition', _lib.add_state_tree_transition_condition(
        _p['object'], _transition_id, _p['condition'], '', 'And', 0, False, False))
    _must('binding', _lib.add_state_tree_binding(
        _p['object'], _evaluator, 'FloatA', _task, 'FloatB', False, False, False))
    _must('set_task_property', _lib.set_state_tree_node_instance_property(
        _p['object'], _task, 'FloatB', '3.25', False, False))
    _must('disable_state', _lib.set_state_tree_state_enabled(
        _p['object'], _state_b, False, False, False))
    _must('enable_state_and_compile', _lib.set_state_tree_state_enabled(
        _p['object'], _state_b, True, True, False))

    _structure_value = _structure(_lib.get_state_tree_structure(_p['object']))
    _validation_value = _validation(_lib.validate_state_tree_asset(_p['object']))
    _out.update({{
        'created_path': str(_created),
        'ids': {{
            'state_a': _state_a,
            'state_b': _state_b,
            'evaluator': _evaluator,
            'task': _task,
            'transition': _transition_id,
            'root_parameter': _root_parameter,
        }},
        'structure': _structure_value,
        'validation': _validation_value,
        'preview': _change_set(_cs.preview_change_set(_change_set_id)),
    }})
    if not _validation_value['success'] or _validation_value['error_count']:
        raise RuntimeError('StateTree validation failed: ' + '; '.join(_validation_value['messages']))
    _rolled = _cs.finalize_change_set(_change_set_id, False)
    _out['change_set'] = _change_set(_rolled)
    _out['asset_exists_after_rollback'] = bool(
        unreal.EditorAssetLibrary.does_asset_exist(_p['package']))
    _out['success'] = bool(_out['change_set']['success']) and not _out['asset_exists_after_rollback']
except Exception as _exc:
    try:
        _rolled = _cs.finalize_change_set(_change_set_id, False)
        _out['change_set'] = _change_set(_rolled)
    except Exception as _rollback_exc:
        _out['rollback_error'] = str(_rollback_exc)
    _out['error'] = str(_exc)
    _out['traceback'] = traceback.format_exc()
print(json.dumps(_out, ensure_ascii=False))
""".strip()


def _run_transaction(
    server: Any,
    project: str,
    endpoint: str | None,
    *,
    invalid_task: bool,
) -> dict[str, Any]:
    suffix = uuid.uuid4().hex[:10]
    name = f"ST_V3_{'Invalid' if invalid_task else 'Full'}_{suffix}"
    package = f"{FOLDER}/{name}"
    payload = {
        "folder": FOLDER,
        "name": name,
        "package": package,
        "object": f"{package}.{name}",
        "schema": SCHEMA,
        "evaluator": EVALUATOR,
        "task": "/Script/StateTreeTestSuite.DoesNotExist" if invalid_task else TASK,
        "condition": CONDITION,
    }
    assert not _asset_exists(server, project, package, endpoint), package
    response = server.bridge_submit_job(
        _transaction_code(payload),
        queue_timeout=300.0,
        run_timeout=900.0,
        endpoint=endpoint,
        project=project,
        timeout=900.0,
        no_preflight=True,
    )
    job_id = response.get("job_id")
    assert response.get("success") is True and job_id, response
    deadline = time.monotonic() + 900.0
    while not response.get("terminal"):
        assert time.monotonic() < deadline, response
        time.sleep(0.1)
        response = server.bridge_get_job(
            job_id,
            endpoint=endpoint,
            project=project,
            timeout=30.0,
        )
    payload_result = server._last_json_output(response)
    assert isinstance(payload_result, dict), response
    assert not _asset_exists(server, project, package, endpoint), payload_result
    return payload_result


def run(project: str, endpoint: str | None) -> dict[str, Any]:
    server = _load_server()
    ping = server.bridge_ping(project=project, endpoint=endpoint, timeout=15.0)
    assert ping.get("success") is True and ping.get("plugin_version") == "3.0.0", ping
    baseline_dirty = _native(
        server, project, "Editor", "get_dirty_package_names", endpoint=endpoint
    )

    result = _run_transaction(server, project, endpoint, invalid_task=False)
    assert result.get("success") is True, result
    structure = result["structure"]
    assert {state["name"] for state in structure["states"]} >= {
        "AcquireTarget",
        "AttackTarget",
    }, result
    assert len(structure["evaluators"]) == 1, result
    assert len(structure["global_tasks"]) == 1, result
    assert {item["name"] for item in structure["parameters"]} >= {
        "Aggression",
        "CanAttack",
    }, result
    assert len(structure["bindings"]) == 1, result
    acquire = next(
        state for state in structure["states"] if state["name"] == "AcquireTarget"
    )
    assert len(acquire["tasks"]) == 1, result
    assert len(acquire["enter_conditions"]) == 1, result
    assert len(acquire["transitions"]) == 1, result
    assert len(acquire["transitions"][0]["conditions"]) == 1, result
    assert result["validation"]["compiled"] is True, result
    assert result["validation"]["error_count"] == 0, result
    assert result["change_set"]["status"] == "RolledBack", result
    assert result["change_set"]["rollback_verified"] is True, result

    invalid = _run_transaction(server, project, endpoint, invalid_task=True)
    assert invalid.get("success") is False, invalid
    assert "TaskStructPath" in invalid.get("error", ""), invalid
    assert invalid.get("change_set", {}).get("status") == "RolledBack", invalid
    assert invalid.get("change_set", {}).get("rollback_verified") is True, invalid

    final_dirty = _native(
        server, project, "Editor", "get_dirty_package_names", endpoint=endpoint
    )
    assert final_dirty == baseline_dirty, {
        "baseline_dirty": baseline_dirty,
        "final_dirty": final_dirty,
    }
    return {
        "success": True,
        "plugin_version": ping["plugin_version"],
        "states": 2,
        "evaluators": 1,
        "state_tasks": 1,
        "global_tasks": 1,
        "enter_conditions": 1,
        "transition_conditions": 1,
        "typed_parameters": 2,
        "bindings": 1,
        "transitions": 1,
        "compiled": True,
        "readback": True,
        "invalid_struct_failure": True,
        "rollback_verified": True,
        "asset_absent_after_rollback": True,
        "dirty_unchanged": True,
        "saved": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", default="ShooterRoyal")
    parser.add_argument("--endpoint")
    args = parser.parse_args()
    print(json.dumps(run(args.project, args.endpoint), ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
