#!/usr/bin/env python3
"""
Grouped MCP adapter for UnrealBridge.

This is intentionally thin: it reuses bridge.py discovery, token handling,
transport, and preflight instead of introducing a second bridge protocol. The
adapter exposes a small set of grouped MCP tools so Codex-style clients do not
need to load hundreds of individual UnrealBridge UFUNCTIONs as separate tools.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import textwrap
import uuid
from types import SimpleNamespace
from typing import Any, Dict, Optional

try:
    from mcp.server.fastmcp import FastMCP
except ImportError as exc:  # pragma: no cover - exercised by users without mcp installed
    raise SystemExit(
        "The UnrealBridge MCP adapter requires the Python 'mcp' package. "
        "Install it in the environment used to launch this server."
    ) from exc

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import bridge as bridge_cli  # noqa: E402


DEFAULT_TIMEOUT = 60.0

WRAPPER_CLASS_BY_GROUP = {
    "ai": "AI",
    "anim": "Anim",
    "asset": "Asset",
    "assetfactory": "AssetFactory",
    "asset_factory": "AssetFactory",
    "blueprint": "Blueprint",
    "chooser": "Chooser",
    "curve": "Curve",
    "datatable": "DataTable",
    "editor": "Editor",
    "foliage": "Foliage",
    "gameplay": "Gameplay",
    "gameplayability": "GameplayAbility",
    "gas": "GameplayAbility",
    "gameplaytag": "GameplayTag",
    "tag": "GameplayTag",
    "level": "Level",
    "landscape": "Landscape",
    "material": "Material",
    "navigation": "Navigation",
    "networking": "Networking",
    "niagara": "Niagara",
    "ik": "IK",
    "pcg": "PCG",
    "perf": "Perf",
    "posesearch": "PoseSearch",
    "procedural": "Procedural",
    "property": "Property",
    "reactive": "Reactive",
    "sequencer": "Sequencer",
    "spline": "Spline",
    "state_tree": "StateTree",
    "statetree": "StateTree",
    "struct": "Struct",
    "umg": "UMG",
    "geometry": "Geometry",
}

try:
    with open(bridge_cli._MANIFEST_PATH, encoding="utf-8") as _manifest_stream:
        BRIDGE_MANIFEST = json.load(_manifest_stream)
except (OSError, json.JSONDecodeError):
    BRIDGE_MANIFEST = {"libraries": {}}


def _resolve_manifest_function(library: str, function: str) -> "tuple[str, dict | None]":
    wrapper_class = WRAPPER_CLASS_BY_GROUP.get(library.lower(), library)
    full_library = f"UnrealBridge{wrapper_class}Library"
    entry = (
        BRIDGE_MANIFEST.get("libraries", {})
        .get(full_library, {})
        .get("functions", {})
        .get(function)
    )
    return wrapper_class, entry


def _schema_mismatch(value: Any, schema: Dict[str, Any], path: str) -> Optional[str]:
    expected = schema.get("type")
    expected_types = set(expected if isinstance(expected, list) else [expected])
    expected_types.discard(None)
    if value is None:
        return None if "null" in expected_types else f"{path} must not be null"
    actual = (
        "boolean" if isinstance(value, bool)
        else "integer" if isinstance(value, int)
        else "number" if isinstance(value, float)
        else "string" if isinstance(value, str)
        else "array" if isinstance(value, (list, tuple))
        else "object" if isinstance(value, dict)
        else "unknown"
    )
    compatible = actual in expected_types or (actual == "integer" and "number" in expected_types)
    if expected_types and not compatible:
        return f"{path} expected {sorted(expected_types)}, got {actual}"
    enum_values = schema.get("enum")
    if enum_values and value not in enum_values:
        return f"{path} must be one of {enum_values}, got {value!r}"
    if actual == "array" and isinstance(schema.get("items"), dict):
        for index, item in enumerate(value):
            error = _schema_mismatch(item, schema["items"], f"{path}[{index}]")
            if error:
                return error
    if actual == "object" and isinstance(schema.get("additionalProperties"), dict):
        for key, item in value.items():
            error = _schema_mismatch(item, schema["additionalProperties"], f"{path}.{key}")
            if error:
                return error
    return None


def _validate_call_kwargs(entry: Dict[str, Any], kwargs: Dict[str, Any]) -> Optional[str]:
    params = {param["name"]: param for param in entry.get("params", [])}
    unknown = sorted(set(kwargs) - set(params))
    if unknown:
        return f"unexpected kwargs: {unknown}; valid: {sorted(params)}"
    missing = [
        name for name, param in params.items()
        if not param.get("has_default") and name not in kwargs
    ]
    if missing:
        return f"missing required kwargs: {missing}"
    for name, value in kwargs.items():
        error = _schema_mismatch(value, params[name].get("json_schema") or {}, name)
        if error:
            return error
    return None


def _namespace(
    *,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
    timeout: float = DEFAULT_TIMEOUT,
    discovery_timeout: Optional[int] = None,
    discovery_group: Optional[str] = None,
    no_preflight: bool = False,
) -> argparse.Namespace:
    return argparse.Namespace(
        endpoint=endpoint,
        project=project,
        token=token,
        timeout=timeout,
        discovery_timeout=discovery_timeout,
        discovery_group=discovery_group,
        no_preflight=no_preflight,
        json=True,
    )


def _send_command(
    payload: Dict[str, Any],
    *,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
    timeout: float = DEFAULT_TIMEOUT,
    discovery_timeout: Optional[int] = None,
    discovery_group: Optional[str] = None,
) -> Dict[str, Any]:
    args = _namespace(
        endpoint=endpoint,
        project=project,
        token=token,
        timeout=timeout,
        discovery_timeout=discovery_timeout,
        discovery_group=discovery_group,
    )
    try:
        host, port, resolved_token, _project_path = bridge_cli.resolve_target(args)
        return bridge_cli.send_request(host, port, payload, timeout + 5, token=resolved_token)
    except SystemExit as exc:
        # bridge.py uses SystemExit for CLI-facing target-resolution errors. An
        # MCP tool failure must not terminate the long-lived stdio server.
        return {
            "success": False,
            "error": str(exc),
            "phase": "target_resolution",
            "retryable": True,
        }
    except (OSError, RuntimeError, ValueError) as exc:
        return {
            "success": False,
            "error": str(exc),
            "phase": "transport",
            "retryable": True,
        }


def _execute_code(
    code: str,
    *,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
    timeout: float = DEFAULT_TIMEOUT,
    discovery_timeout: Optional[int] = None,
    discovery_group: Optional[str] = None,
    no_preflight: bool = False,
    queue_timeout: Optional[float] = None,
    idempotency_key: Optional[str] = None,
) -> Dict[str, Any]:
    if not no_preflight:
        errors, warnings = bridge_cli._preflight_or_skip(code)
        if errors:
            return {
                "success": False,
                "output": "",
                "error": "\n".join(errors),
                "warnings": warnings,
                "phase": "preflight",
            }
    else:
        warnings = []

    wrapped = bridge_cli._wrap_for_attr_enrichment(code)
    payload = {
        "id": str(uuid.uuid4()),
        "script": wrapped,
        "timeout": timeout,
        "wait_timeout": timeout,
        "queue_timeout": queue_timeout if queue_timeout is not None else timeout,
        **bridge_cli.client_handshake(),
    }
    if idempotency_key:
        payload["idempotency_key"] = idempotency_key
    result = _send_command(
        payload,
        endpoint=endpoint,
        project=project,
        token=token,
        timeout=timeout,
        discovery_timeout=discovery_timeout,
        discovery_group=discovery_group,
    )
    if warnings:
        result = dict(result)
        result["warnings"] = warnings
    return result


def _call_code(
    wrapper_class: str,
    function_name: str,
    kwargs: Dict[str, Any],
    param_schemas: Optional[Dict[str, Dict[str, Any]]] = None,
) -> str:
    payload = json.dumps(kwargs or {}, ensure_ascii=False)
    schemas = json.dumps(param_schemas or {}, ensure_ascii=False)
    return textwrap.dedent(
        f"""
        import json
        import unreal
        from unreal_bridge import {wrapper_class} as _UBClass

        def _ub_jsonable(value):
            if value is None or isinstance(value, (bool, int, float, str)):
                return value
            if isinstance(value, (list, tuple)):
                return [_ub_jsonable(v) for v in value]
            if isinstance(value, dict):
                return {{str(k): _ub_jsonable(v) for k, v in value.items()}}
            if hasattr(value, "export_text"):
                try:
                    return value.export_text()
                except Exception:
                    pass
            if hasattr(value, "get_path_name"):
                try:
                    return value.get_path_name()
                except Exception:
                    pass
            return str(value)

        def _ub_coerce(value, schema):
            if value is None:
                return None
            enum_path = schema.get('x-unreal-enum')
            if enum_path and isinstance(value, str):
                enum_name = enum_path.rsplit('.', 1)[-1]
                enum_cls = getattr(unreal, enum_name, None)
                if enum_cls is None and enum_name.startswith('E'):
                    enum_cls = getattr(unreal, enum_name[1:], None)
                if enum_cls is not None:
                    return getattr(enum_cls, value)
            struct_name = schema.get('x-unreal-struct')
            if struct_name:
                struct_cls = getattr(unreal, struct_name, None)
                if struct_cls is not None and isinstance(value, dict):
                    return struct_cls(**value)
                if struct_cls is not None and isinstance(value, (list, tuple)):
                    return struct_cls(*value)
            if schema.get('format') == 'unreal-class-path' and isinstance(value, str):
                return unreal.load_class(None, value)
            if schema.get('format') == 'unreal-object-path' and isinstance(value, str):
                return unreal.load_object(None, value)
            if isinstance(value, list) and isinstance(schema.get('items'), dict):
                return [_ub_coerce(item, schema['items']) for item in value]
            if isinstance(value, dict) and isinstance(schema.get('additionalProperties'), dict):
                return {{key: _ub_coerce(item, schema['additionalProperties']) for key, item in value.items()}}
            return value

        _kwargs = json.loads({payload!r})
        _schemas = json.loads({schemas!r})
        _kwargs = {{key: _ub_coerce(value, _schemas.get(key, {{}})) for key, value in _kwargs.items()}}
        _fn = getattr(_UBClass, {function_name!r})
        _result = _fn(**_kwargs)
        print(json.dumps({{"ok": True, "result": _ub_jsonable(_result)}}, ensure_ascii=False))
        """
    ).strip()


mcp = FastMCP("unreal-bridge")


@mcp.tool()
def bridge_ping(
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
    timeout: float = 10.0,
) -> Dict[str, Any]:
    """Check whether a running UnrealBridge editor is reachable."""
    return _send_command(
        {"id": str(uuid.uuid4()), "command": "ping"},
        endpoint=endpoint,
        project=project,
        token=token,
        timeout=timeout,
    )


@mcp.tool()
def bridge_health(
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
    timeout: float = 10.0,
) -> Dict[str, Any]:
    """Return queue depth, running job, client count, and lifetime metrics."""
    return _send_command(
        {"id": str(uuid.uuid4()), "command": "health"},
        endpoint=endpoint, project=project, token=token, timeout=timeout,
    )


@mcp.tool()
def bridge_describe(
    library: str,
    function: str,
) -> Dict[str, Any]:
    """Return the strict schema and risk metadata for one grouped operation."""
    wrapper_class, entry = _resolve_manifest_function(library, function)
    if entry is None:
        return {
            "success": False,
            "error": f"unknown UnrealBridge operation {library}.{function}",
        }
    return {
        "success": True,
        "library": wrapper_class,
        "function": function,
        "schema": entry,
    }


@mcp.tool()
def bridge_submit_job(
    code: str,
    idempotency_key: Optional[str] = None,
    queue_timeout: float = 300.0,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
    timeout: float = 15.0,
    no_preflight: bool = False,
) -> Dict[str, Any]:
    """Submit Python as a durable UE job and return immediately with job_id."""
    if not no_preflight:
        errors, warnings = bridge_cli._preflight_or_skip(code)
        if errors:
            return {"success": False, "error": "\n".join(errors), "warnings": warnings, "phase": "preflight"}
    else:
        warnings = []
    payload: Dict[str, Any] = {
        "id": str(uuid.uuid4()),
        "command": "submit_job",
        "script": bridge_cli._wrap_for_attr_enrichment(code),
        "queue_timeout": queue_timeout,
        **bridge_cli.client_handshake(),
    }
    if idempotency_key:
        payload["idempotency_key"] = idempotency_key
    result = _send_command(
        payload, endpoint=endpoint, project=project, token=token, timeout=timeout,
    )
    if warnings:
        result = dict(result)
        result["warnings"] = warnings
    return result


@mcp.tool()
def bridge_get_job(
    job_id: str,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
    timeout: float = 10.0,
) -> Dict[str, Any]:
    """Get the current state and retained result of a durable UE job."""
    return _send_command(
        {"id": str(uuid.uuid4()), "command": "get_job", "job_id": job_id},
        endpoint=endpoint, project=project, token=token, timeout=timeout,
    )


@mcp.tool()
def bridge_wait_job(
    job_id: str,
    wait_timeout: float = 30.0,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
) -> Dict[str, Any]:
    """Wait for a durable job without changing its lifecycle on client timeout."""
    return _send_command(
        {
            "id": str(uuid.uuid4()),
            "command": "wait_job",
            "job_id": job_id,
            "wait_timeout": wait_timeout,
        },
        endpoint=endpoint, project=project, token=token, timeout=wait_timeout + 5.0,
    )


@mcp.tool()
def bridge_cancel_job(
    job_id: str,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
    timeout: float = 10.0,
) -> Dict[str, Any]:
    """Cancel a queued job or request cooperative cancellation of a running job."""
    return _send_command(
        {"id": str(uuid.uuid4()), "command": "cancel_job", "job_id": job_id},
        endpoint=endpoint, project=project, token=token, timeout=timeout,
    )


@mcp.tool()
def bridge_list_jobs(
    limit: int = 50,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
    timeout: float = 10.0,
) -> Dict[str, Any]:
    """List recent durable UE jobs and their states."""
    return _send_command(
        {"id": str(uuid.uuid4()), "command": "list_jobs", "limit": limit},
        endpoint=endpoint, project=project, token=token, timeout=timeout,
    )


@mcp.tool()
def bridge_exec(
    code: str,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
    timeout: float = DEFAULT_TIMEOUT,
    no_preflight: bool = False,
    queue_timeout: Optional[float] = None,
    idempotency_key: Optional[str] = None,
) -> Dict[str, Any]:
    """Execute Python code in the running Unreal Editor."""
    return _execute_code(
        code,
        endpoint=endpoint,
        project=project,
        token=token,
        timeout=timeout,
        no_preflight=no_preflight,
        queue_timeout=queue_timeout,
        idempotency_key=idempotency_key,
    )


@mcp.tool()
def bridge_call(
    library: str,
    function: str,
    kwargs: Optional[Dict[str, Any]] = None,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
    timeout: float = DEFAULT_TIMEOUT,
    no_preflight: bool = False,
) -> Dict[str, Any]:
    """Call a generated unreal_bridge wrapper class function by grouped name."""
    wrapper_class, entry = _resolve_manifest_function(library, function)
    if entry is None:
        return {
            "success": False,
            "error": f"unknown UnrealBridge operation {library}.{function}",
            "phase": "schema",
            "retryable": False,
        }
    validation_error = _validate_call_kwargs(entry, kwargs or {})
    if validation_error:
        return {
            "success": False,
            "error": validation_error,
            "phase": "schema",
            "retryable": False,
        }
    param_schemas = {
        param["name"]: param.get("json_schema") or {}
        for param in entry.get("params", [])
    }
    return _execute_code(
        _call_code(wrapper_class, function, kwargs or {}, param_schemas),
        endpoint=endpoint,
        project=project,
        token=token,
        timeout=timeout,
        no_preflight=no_preflight,
    )


def _group_tool(
    group: str,
    op: str,
    kwargs: Optional[Dict[str, Any]],
    endpoint: Optional[str],
    project: Optional[str],
    token: Optional[str],
    timeout: float,
    no_preflight: bool,
) -> Dict[str, Any]:
    return bridge_call(
        group,
        op,
        kwargs or {},
        endpoint=endpoint,
        project=project,
        token=token,
        timeout=timeout,
        no_preflight=no_preflight,
    )


@mcp.tool()
def asset_op(
    op: str,
    kwargs: Optional[Dict[str, Any]] = None,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
    timeout: float = DEFAULT_TIMEOUT,
    no_preflight: bool = False,
) -> Dict[str, Any]:
    """Call an UnrealBridge Asset operation."""
    return _group_tool("asset", op, kwargs, endpoint, project, token, timeout, no_preflight)


@mcp.tool()
def asset_factory_op(
    op: str,
    kwargs: Optional[Dict[str, Any]] = None,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
    timeout: float = DEFAULT_TIMEOUT,
    no_preflight: bool = False,
) -> Dict[str, Any]:
    """Call an UnrealBridge AssetFactory operation."""
    return _group_tool("assetfactory", op, kwargs, endpoint, project, token, timeout, no_preflight)


@mcp.tool()
def level_op(
    op: str,
    kwargs: Optional[Dict[str, Any]] = None,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
    timeout: float = DEFAULT_TIMEOUT,
    no_preflight: bool = False,
) -> Dict[str, Any]:
    """Call an UnrealBridge Level operation."""
    return _group_tool("level", op, kwargs, endpoint, project, token, timeout, no_preflight)


@mcp.tool()
def blueprint_op(
    op: str,
    kwargs: Optional[Dict[str, Any]] = None,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
    timeout: float = DEFAULT_TIMEOUT,
    no_preflight: bool = False,
) -> Dict[str, Any]:
    """Call an UnrealBridge Blueprint operation."""
    return _group_tool("blueprint", op, kwargs, endpoint, project, token, timeout, no_preflight)


@mcp.tool()
def editor_op(
    op: str,
    kwargs: Optional[Dict[str, Any]] = None,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
    timeout: float = DEFAULT_TIMEOUT,
    no_preflight: bool = False,
) -> Dict[str, Any]:
    """Call an UnrealBridge Editor operation."""
    return _group_tool("editor", op, kwargs, endpoint, project, token, timeout, no_preflight)


@mcp.tool()
def umg_op(
    op: str,
    kwargs: Optional[Dict[str, Any]] = None,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
    timeout: float = DEFAULT_TIMEOUT,
    no_preflight: bool = False,
) -> Dict[str, Any]:
    """Call an UnrealBridge UMG operation."""
    return _group_tool("umg", op, kwargs, endpoint, project, token, timeout, no_preflight)


@mcp.tool()
def ai_op(
    op: str,
    kwargs: Optional[Dict[str, Any]] = None,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
    timeout: float = DEFAULT_TIMEOUT,
    no_preflight: bool = False,
) -> Dict[str, Any]:
    """Call an UnrealBridge AI operation."""
    return _group_tool("ai", op, kwargs, endpoint, project, token, timeout, no_preflight)


@mcp.tool()
def niagara_op(
    op: str,
    kwargs: Optional[Dict[str, Any]] = None,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
    timeout: float = DEFAULT_TIMEOUT,
    no_preflight: bool = False,
) -> Dict[str, Any]:
    """Call an UnrealBridge Niagara operation."""
    return _group_tool("niagara", op, kwargs, endpoint, project, token, timeout, no_preflight)


@mcp.tool()
def sequencer_op(
    op: str,
    kwargs: Optional[Dict[str, Any]] = None,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
    timeout: float = DEFAULT_TIMEOUT,
    no_preflight: bool = False,
) -> Dict[str, Any]:
    """Call an UnrealBridge Sequencer operation."""
    return _group_tool("sequencer", op, kwargs, endpoint, project, token, timeout, no_preflight)


@mcp.tool()
def datatable_op(
    op: str,
    kwargs: Optional[Dict[str, Any]] = None,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
    timeout: float = DEFAULT_TIMEOUT,
    no_preflight: bool = False,
) -> Dict[str, Any]:
    """Call an UnrealBridge DataTable operation."""
    return _group_tool("datatable", op, kwargs, endpoint, project, token, timeout, no_preflight)


@mcp.tool()
def gas_op(
    op: str,
    kwargs: Optional[Dict[str, Any]] = None,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
    timeout: float = DEFAULT_TIMEOUT,
    no_preflight: bool = False,
) -> Dict[str, Any]:
    """Call an UnrealBridge GameplayAbility/GAS operation."""
    return _group_tool("gas", op, kwargs, endpoint, project, token, timeout, no_preflight)


@mcp.tool()
def gameplaytag_op(
    op: str,
    kwargs: Optional[Dict[str, Any]] = None,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
    timeout: float = DEFAULT_TIMEOUT,
    no_preflight: bool = False,
) -> Dict[str, Any]:
    """Call an UnrealBridge GameplayTag operation."""
    return _group_tool("gameplaytag", op, kwargs, endpoint, project, token, timeout, no_preflight)


def _camel_to_snake(value: str) -> str:
    chars = []
    for index, char in enumerate(value):
        if char.isupper() and index > 0:
            previous = value[index - 1]
            next_is_lower = index + 1 < len(value) and value[index + 1].islower()
            if (previous.islower() or previous.isdigit() or (previous.isupper() and next_is_lower)):
                chars.append("_")
        chars.append(char.lower())
    return "".join(chars)


def _register_manifest_group_tools() -> None:
    """Expose one compact operation tool per manifest library, never per UFUNCTION."""
    existing = {
        "ai_op", "asset_op", "asset_factory_op", "blueprint_op", "datatable_op",
        "editor_op", "gas_op", "gameplaytag_op", "level_op", "niagara_op",
        "sequencer_op", "umg_op", "data_table_op", "gameplay_ability_op",
        "gameplay_tag_op",
    }
    for full_name in sorted(BRIDGE_MANIFEST.get("libraries", {})):
        if not (full_name.startswith("UnrealBridge") and full_name.endswith("Library")):
            continue
        short_name = full_name[len("UnrealBridge"):-len("Library")]
        tool_name = f"{_camel_to_snake(short_name)}_op"
        if tool_name in existing:
            continue

        generated_group_tool = _make_manifest_group_tool(short_name)
        generated_group_tool.__name__ = tool_name
        generated_group_tool.__doc__ = f"Call an UnrealBridge {short_name} operation."
        mcp.tool(name=tool_name)(generated_group_tool)


def _make_manifest_group_tool(group: str):
    def generated_group_tool(
        op: str,
        kwargs: Optional[Dict[str, Any]] = None,
        endpoint: Optional[str] = None,
        project: Optional[str] = None,
        token: Optional[str] = None,
        timeout: float = DEFAULT_TIMEOUT,
        no_preflight: bool = False,
    ) -> Dict[str, Any]:
        return _group_tool(
            group, op, kwargs, endpoint, project, token, timeout, no_preflight
        )

    return generated_group_tool


_register_manifest_group_tools()


if __name__ == "__main__":
    mcp.run()
