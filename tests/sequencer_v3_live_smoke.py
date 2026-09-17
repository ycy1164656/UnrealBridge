"""Deep Sequencer binding/track/section/key acceptance on UE 5.8.

Two unsaved preview transactions are exercised.  The native high-level path
creates a possessable actor binding, transform/property tracks, sections, and
keys with structural readback.  The audited Epic Toolset path creates a
spawnable binding.  Both assets are rolled back and never saved.
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


def _load_server() -> Any:
    spec = importlib.util.spec_from_file_location(
        "unreal_bridge_sequencer_live", SERVER_PATH
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


def _native_transaction_code(payload: dict[str, Any]) -> str:
    encoded = base64.b64encode(
        json.dumps(payload, ensure_ascii=False, sort_keys=True).encode("utf-8")
    ).decode("ascii")
    return f"""
import base64, json, traceback, unreal
_p = json.loads(base64.b64decode({encoded!r}).decode('utf-8'))
_lib = unreal.UnrealBridgeSequencerLibrary
_cs = unreal.UnrealBridgeChangeSetLibrary

def _a(value, name, default=None):
    try:
        return getattr(value, name)
    except Exception:
        return default

def _binding(value):
    return {{
        'binding_name': str(_a(value, 'binding_name', '')),
        'binding_id': str(_a(value, 'binding_id', '')),
    }}

def _track(value):
    return {{
        'binding_name': str(_a(value, 'binding_name', '')),
        'binding_id': str(_a(value, 'binding_id', '')),
        'track_name': str(_a(value, 'track_name', '')),
        'track_class': str(_a(value, 'track_class', '')),
    }}

def _section(value):
    return {{
        'binding_name': str(_a(value, 'binding_name', '')),
        'binding_id': str(_a(value, 'binding_id', '')),
        'track_name': str(_a(value, 'track_name', '')),
        'track_class': str(_a(value, 'track_class', '')),
        'section_class': str(_a(value, 'section_class', '')),
        'section_index': int(_a(value, 'section_index', -1)),
        'start_seconds': float(_a(value, 'start_seconds', 0.0) or 0.0),
        'end_seconds': float(_a(value, 'end_seconds', 0.0) or 0.0),
        'has_start': bool(_a(value, 'has_start', False)),
        'has_end': bool(_a(value, 'has_end', False)),
        'channel_count': int(_a(value, 'channel_count', 0) or 0),
        'key_count': int(_a(value, 'key_count', 0) or 0),
    }}

def _validation(value):
    return {{
        'success': bool(_a(value, 'success', False)),
        'error_count': int(_a(value, 'error_count', 0) or 0),
        'warning_count': int(_a(value, 'warning_count', 0) or 0),
        'messages': [str(x) for x in (_a(value, 'messages', []) or [])],
    }}

def _change_set(value):
    return {{
        'status': str(_a(value, 'status', '')),
        'success': bool(_a(value, 'success', False)),
        'saved': bool(_a(value, 'saved', False)),
        'dirty_packages_for_job': list(_a(value, 'dirty_packages_for_job', []) or []),
        'unexpected_dirty_packages': list(_a(value, 'unexpected_dirty_packages', []) or []),
        'created_assets_for_job': list(_a(value, 'created_assets_for_job', []) or []),
        'removed_created_assets_during_rollback': list(_a(value, 'removed_created_assets_during_rollback', []) or []),
        'rollback_verified': bool(_a(value, 'rollback_verified', False)),
        'error': str(_a(value, 'error', '')),
    }}

_change_set_id = _cs.begin_change_set(
    'UnrealBridge 3.0 Sequencer smoke', [_p['package']]
)
_out = {{'success': False, 'save_behavior': 'Never'}}
try:
    _created = _lib.create_level_sequence(_p['folder'], _p['name'], False)
    if not _created:
        raise RuntimeError('CreateLevelSequence returned an empty path')
    _binding_value = _binding(_lib.add_actor_binding(_p['object'], _p['actor_label']))
    if not _binding_value['binding_id']:
        raise RuntimeError('AddActorBinding could not resolve actor label: ' + _p['actor_label'])
    _transform_track = str(_lib.add_transform_track(
        _p['object'], _p['actor_label'], 0.0, 2.0))
    if not _transform_track:
        raise RuntimeError('AddTransformTrack returned an empty path')
    if not _lib.set_playback_range(_p['object'], 0.0, 2.0):
        raise RuntimeError('SetPlaybackRange failed')
    for _seconds, _x in ((0.0, 0.0), (0.5, 100.0), (1.0, 200.0)):
        if not _lib.add_transform_key(
            _p['object'], _p['actor_label'], _seconds,
            unreal.Vector(_x, 25.0, 50.0), unreal.Rotator(0.0, _x / 2.0, 0.0),
            unreal.Vector(1.0, 1.0, 1.0), True, 'Auto'):
            raise RuntimeError('AddTransformKey failed at ' + str(_seconds))
    _property_track = str(_lib.add_property_track(
        _p['object'], _p['actor_label'], 'bHidden', 'bHidden',
        unreal.BridgeSequencerPropertyType.BOOL, 0.0, 2.0, False))
    if not _property_track:
        raise RuntimeError('AddPropertyTrack returned an empty path')
    for _seconds, _value in ((0.0, 'False'), (0.5, 'True'), (1.0, 'False')):
        if not _lib.add_property_key(
            _p['object'], _p['actor_label'], 'bHidden', 'bHidden',
            unreal.BridgeSequencerPropertyType.BOOL, _seconds, _value,
            True, 'Auto', False):
            raise RuntimeError('AddPropertyKey failed at ' + str(_seconds))
    _tracks = [_track(x) for x in (_lib.list_sequence_tracks(_p['object']) or [])]
    _sections = [_section(x) for x in (_lib.list_sequence_sections(_p['object']) or [])]
    _validation_value = _validation(_lib.validate_level_sequence(_p['object']))
    _out.update({{
        'created_path': str(_created),
        'binding': _binding_value,
        'transform_track': _transform_track,
        'property_track': _property_track,
        'tracks': _tracks,
        'sections': _sections,
        'validation': _validation_value,
        'preview': _change_set(_cs.preview_change_set(_change_set_id)),
    }})
    if not _validation_value['success'] or _validation_value['error_count']:
        raise RuntimeError('LevelSequence validation failed: ' + '; '.join(_validation_value['messages']))
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


def _wait_job(
    server: Any,
    response: dict[str, Any],
    project: str,
    endpoint: str | None,
    timeout: float = 900.0,
) -> dict[str, Any]:
    job_id = response.get("job_id")
    assert response.get("success") is True and job_id, response
    deadline = time.monotonic() + timeout
    while not response.get("terminal"):
        assert time.monotonic() < deadline, response
        time.sleep(0.1)
        response = server.bridge_get_job(
            job_id,
            endpoint=endpoint,
            project=project,
            timeout=30.0,
        )
    payload = server._last_json_output(response)
    assert isinstance(payload, dict), response
    return payload


def _run_native(
    server: Any,
    project: str,
    endpoint: str | None,
    actor_label: str,
) -> dict[str, Any]:
    suffix = uuid.uuid4().hex[:10]
    name = f"LS_V3Native_{suffix}"
    package = f"{FOLDER}/{name}"
    payload = {
        "folder": FOLDER,
        "name": name,
        "package": package,
        "object": f"{package}.{name}",
        "actor_label": actor_label,
    }
    assert not _asset_exists(server, project, package, endpoint), package
    result = _wait_job(
        server,
        server.bridge_submit_job(
            _native_transaction_code(payload),
            queue_timeout=300.0,
            run_timeout=900.0,
            endpoint=endpoint,
            project=project,
            timeout=900.0,
            no_preflight=True,
        ),
        project,
        endpoint,
    )
    assert not _asset_exists(server, project, package, endpoint), result
    return result


def _run_spawnable(
    server: Any, project: str, endpoint: str | None
) -> dict[str, Any]:
    suffix = uuid.uuid4().hex[:10]
    name = f"LS_V3Spawnable_{suffix}"
    package = f"{FOLDER}/{name}"
    sequence = f"{package}.{name}"
    assert not _asset_exists(server, project, package, endpoint), package
    response = server.bridge_submit_sequencer_workflow(
        calls=[
            {
                "operation": "epic:animation_toolset.toolsets.sequencer.SequencerTools.create_level_sequence",
                "arguments": {"package_path": FOLDER, "asset_name": name},
            },
            {
                "operation": "epic:animation_toolset.toolsets.sequencer.SequencerTools.add_spawnable_from_class",
                "arguments": {
                    "sequence": {"refPath": sequence},
                    "actor_class_path": "/Script/Engine.StaticMeshActor",
                },
            },
            {
                "operation": "epic:animation_toolset.toolsets.sequencer.SequencerTools.get_bindings",
                "arguments": {"sequence": {"refPath": sequence}},
            },
        ],
        target_packages=[package],
        apply=False,
        endpoint=endpoint,
        project=project,
        timeout=300.0,
    )
    assert response.get("success") is True, response
    official = response.get("official_result") or {}
    assert official.get("success") is True, response
    change_set = official.get("change_set") or {}
    assert change_set.get("status") == "RolledBack", response
    assert change_set.get("rollback_verified") is True, response
    calls = official.get("calls") or []
    assert len(calls) == 3, response
    assert "bindingId" in json.dumps(calls[1:], ensure_ascii=False), response
    assert not _asset_exists(server, project, package, endpoint), response
    return {
        "call_count": len(calls),
        "binding_readback": True,
        "rollback_verified": True,
    }


def run(
    project: str, endpoint: str | None, actor_label: str
) -> dict[str, Any]:
    server = _load_server()
    ping = server.bridge_ping(project=project, endpoint=endpoint, timeout=15.0)
    assert ping.get("success") is True and ping.get("plugin_version") == "3.0.0", ping
    baseline_dirty = _native(
        server, project, "Editor", "get_dirty_package_names", endpoint=endpoint
    )

    native = _run_native(server, project, endpoint, actor_label)
    assert native.get("success") is True, native
    assert native["binding"]["binding_name"] == actor_label, native
    assert len(native["tracks"]) == 2, native
    assert len(native["sections"]) == 2, native
    assert sum(section["key_count"] for section in native["sections"]) >= 6, native
    assert native["validation"]["error_count"] == 0, native
    assert native["change_set"]["status"] == "RolledBack", native
    assert native["change_set"]["rollback_verified"] is True, native

    invalid = _run_native(
        server, project, endpoint, f"UB_MissingActor_{uuid.uuid4().hex}"
    )
    assert invalid.get("success") is False, invalid
    assert "AddActorBinding" in invalid.get("error", ""), invalid
    assert invalid.get("change_set", {}).get("status") == "RolledBack", invalid
    assert invalid.get("change_set", {}).get("rollback_verified") is True, invalid

    spawnable = _run_spawnable(server, project, endpoint)
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
        "possessable_binding": True,
        "spawnable_binding": spawnable["binding_readback"],
        "tracks": len(native["tracks"]),
        "sections": len(native["sections"]),
        "key_count": sum(
            section["key_count"] for section in native["sections"]
        ),
        "validation_error_count": native["validation"]["error_count"],
        "invalid_actor_failure": True,
        "rollback_verified": True,
        "asset_absent_after_rollback": True,
        "dirty_unchanged": True,
        "saved": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", default="ShooterRoyal")
    parser.add_argument("--endpoint")
    parser.add_argument("--actor-label", default="DirectionalLight")
    args = parser.parse_args()
    print(
        json.dumps(
            run(args.project, args.endpoint, args.actor_label),
            ensure_ascii=False,
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
