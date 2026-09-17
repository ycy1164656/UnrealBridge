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
import base64
import copy
import contextvars
import functools
import hashlib
import inspect
import json
import os
import re
import sys
import textwrap
import threading
import time
import uuid
from collections import OrderedDict
from pathlib import Path
from types import SimpleNamespace
from typing import Any, Dict, List, Optional

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
from unreal_bridge_catalog import CatalogCache, CatalogUnavailable, JsonFileCache, digest as catalog_digest, project_identity  # noqa: E402
from unreal_bridge_domains import OfficialDomainRegistry  # noqa: E402
from unreal_bridge_project_context import ProjectContextIndex, ContextError, build_context  # noqa: E402
import unreal_bridge_graph_codec as graph_codec  # noqa: E402
import unreal_bridge_runtime as runtime_recipes  # noqa: E402
import unreal_bridge_network_sessions as network_sessions  # noqa: E402
import unreal_bridge_external_mcp as external_mcp  # noqa: E402
import unreal_bridge_pointer_input as pointer_input  # noqa: E402
import unreal_bridge_authoring as authoring  # noqa: E402
import unreal_bridge_audio_sessions as audio_sessions  # noqa: E402
import unreal_bridge_upgrade as upgrade_contract  # noqa: E402
try:
    from unreal_bridge_golden import compare_images as compare_golden_images  # noqa: E402
    GOLDEN_IMAGE_IMPORT_ERROR = ""
except ImportError as exc:  # Keep non-visual bridge tools available in minimal runtimes.
    compare_golden_images = None
    GOLDEN_IMAGE_IMPORT_ERROR = str(exc)
from unreal_bridge_niagara import (  # noqa: E402
    SYSTEM_TOOLSET as NIAGARA_SYSTEM_TOOLSET,
    add_user_variables_call,
    dynamic_input_data,
    emitter_identity_guard_code,
    existing_binding_kind,
    linked_input_data,
    literal_input_data,
    object_ref,
    package_from_object_path,
    rename_emitter_call,
    set_stack_input_call,
    stack_item_reference,
    type_definition,
)
from unreal_bridge_workflows import (  # noqa: E402
    ArtifactStore,
    ScenarioManager,
    ToolIndex,
    last_json_object,
    shape_result,
)


DEFAULT_TIMEOUT = 60.0
DEFAULT_RESULT_ARTIFACT_THRESHOLD = 256 * 1024

WRAPPER_CLASS_BY_GROUP = {
    "upgrade": "Upgrade",
    "network_session": "NetworkSession",
    "slate_input": "SlateInput",
    "change_set": "ChangeSet",
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
    "game_feature": "GameFeature",
    "gamefeature": "GameFeature",
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
    "world": "World",
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
    "smart_object": "SmartObject",
    "smartobject": "SmartObject",
    "audio": "Audio",
    "world_partition": "WorldPartition",
    "worldpartition": "WorldPartition",
    "struct": "Struct",
    "umg": "UMG",
    "geometry": "Geometry",
}

try:
    with open(bridge_cli._MANIFEST_PATH, encoding="utf-8") as _manifest_stream:
        BRIDGE_MANIFEST = json.load(_manifest_stream)
except (OSError, json.JSONDecodeError):
    BRIDGE_MANIFEST = {"libraries": {}}


def _load_official_catalog_fixture() -> Any:
    """Load the audited compact/live catalog when the checkout ships one."""
    candidates = [
        Path(SCRIPT_DIR) / "official_tool_catalog.json",
        Path(SCRIPT_DIR).parents[3] / "tests" / "fixtures" / "ue58-toolsets.json",
    ]
    for path in candidates:
        try:
            with path.open(encoding="utf-8") as stream:
                return json.load(stream)
        except (OSError, json.JSONDecodeError):
            continue
    return None


OFFICIAL_TOOL_CATALOG = _load_official_catalog_fixture()
TOOL_INDEX = ToolIndex(BRIDGE_MANIFEST, OFFICIAL_TOOL_CATALOG)
OFFICIAL_DOMAIN_REGISTRY = OfficialDomainRegistry(OFFICIAL_TOOL_CATALOG)
_SCENARIO_MANAGERS: Dict[str, ScenarioManager] = {}
_SCENARIO_MANAGER_LOCK = threading.RLock()

_CATALOG_CACHE = CatalogCache()
_CATALOG_FILES = JsonFileCache()
_CATALOG_CONTEXT = contextvars.ContextVar("unreal_bridge_catalog", default=None)
_PROJECT_CONTEXT_INDICES = OrderedDict()
_PROJECT_CONTEXT_LOCK = threading.Lock()


def _current_catalog():
    binding = _CATALOG_CONTEXT.get()
    if binding is not None:
        return binding.snapshot
    # Compatibility reference for direct internal inspection. Public discovery
    # and all official execution entry points bind a live snapshot explicitly.
    return SimpleNamespace(tool_index=TOOL_INDEX, domain_registry=OFFICIAL_DOMAIN_REGISTRY,
                           manifest=_CATALOG_FILES.read(bridge_cli._MANIFEST_PATH))


def _catalog_snapshot_for(endpoint=None, project=None, token=None, *, for_execution=False):
    current = _CATALOG_CONTEXT.get()
    if (current is not None and project in (None, current.project, current.selector_project)
            and endpoint in (None, current.endpoint, current.selector_endpoint)
            and token in (None, current.token)
            and (not for_execution or not current.snapshot.stale)):
        return current
    manifest = _CATALOG_FILES.read(bridge_cli._MANIFEST_PATH)
    audited = _CATALOG_FILES.read(Path(SCRIPT_DIR) / "official_tool_catalog.json")
    files_digest = catalog_digest([_CATALOG_FILES.fingerprint(manifest), _CATALOG_FILES.fingerprint(audited)])
    try:
        args = _namespace(endpoint=endpoint, project=project, token=token, timeout=10.0)
        host, port, resolved_token, resolved_project = bridge_cli.resolve_target(args)
        resolved_endpoint = f"[{host}]:{port}" if ":" in host else f"{host}:{port}"
        key = (project_identity(resolved_project), resolved_endpoint)
        def fetch(include):
            # A nested request may deliberately select a different Editor.
            # Its metadata query must not inherit the previous Editor's guard.
            context_token = _CATALOG_CONTEXT.set(None)
            try:
                response = _execute_code(
                    "import unreal\nprint(unreal.UnrealBridgeUE58Library.get_official_toolset_catalog_snapshot_json("
                    + repr(include) + "))",
                    endpoint=resolved_endpoint, project=resolved_project, token=resolved_token,
                    timeout=10.0, no_preflight=True,
                )
            finally:
                _CATALOG_CONTEXT.reset(context_token)
            if not response.get("success"):
                raise CatalogUnavailable(str(response.get("error") or "Catalog query failed"))
            return _last_json_output(response)
        effective_fetch = fetch
        if not resolved_project:
            # Explicit endpoints intentionally bypass CLI discovery. Bind them
            # to the project reported by the authenticated native snapshot.
            first_meta = fetch(False)
            actual_project = first_meta.get("project_path", "") if isinstance(first_meta, dict) else ""
            if not isinstance(actual_project, str) or not actual_project.lower().endswith(".uproject"):
                raise CatalogUnavailable("Explicit endpoint did not identify its project")
            requested = project or os.environ.get("UNREAL_BRIDGE_PROJECT")
            if requested:
                path_like = Path(requested).is_absolute() or str(requested).lower().endswith(".uproject")
                matches = project_identity(requested) == project_identity(actual_project) if path_like else str(requested).casefold() in Path(actual_project).stem.casefold()
                if not matches:
                    raise CatalogUnavailable("Explicit endpoint belongs to a different project")
            resolved_project = actual_project
            key = (project_identity(actual_project), resolved_endpoint)
            effective_fetch = lambda include: fetch(True) if include else first_meta
        snapshot = _CATALOG_CACHE.get(key, manifest, audited, effective_fetch,
                                      for_execution=for_execution, files_digest=files_digest)
    except (SystemExit, OSError, RuntimeError, ValueError) as exc:
        if for_execution:
            raise CatalogUnavailable(str(exc)) from exc
        resolved_project, resolved_endpoint, resolved_token = project, endpoint, token
        def unavailable(_include):
            raise CatalogUnavailable(str(exc))
        snapshot = _CATALOG_CACHE.get((project_identity(project), endpoint or "unresolved"),
                                      manifest, audited, unavailable, files_digest=files_digest)
    return SimpleNamespace(snapshot=snapshot, project=resolved_project, endpoint=resolved_endpoint,
                           token=resolved_token, selector_project=project, selector_endpoint=endpoint)


def _with_catalog(*, for_execution=True):
    def decorate(function):
        signature = inspect.signature(function)
        @functools.wraps(function)
        def wrapped(*args, **kwargs):
            bound = signature.bind(*args, **kwargs)
            try:
                binding = _catalog_snapshot_for(bound.arguments.get("endpoint"), bound.arguments.get("project"),
                                                bound.arguments.get("token"), for_execution=for_execution)
            except CatalogUnavailable as exc:
                return {"success": False, "phase": "catalog", "retryable": True,
                        "error_code": "live_catalog_unavailable", "error": str(exc)}
            context_token = _CATALOG_CONTEXT.set(binding)
            try:
                for name in ("endpoint", "project", "token"):
                    if name in signature.parameters:
                        bound.arguments[name] = getattr(binding, name)
                result = function(*bound.args, **bound.kwargs)
                return {**result, "catalog_metadata": binding.snapshot.metadata()}
            finally:
                _CATALOG_CONTEXT.reset(context_token)
        return wrapped
    return decorate


def _catalog_guarded_script(code: str, *, session_only=False) -> str:
    binding = _CATALOG_CONTEXT.get()
    if binding is None or binding.snapshot.stale:
        return code
    snapshot = binding.snapshot
    expected = (snapshot.session,) if session_only else (snapshot.session, snapshot.revision)
    fields = "(_ub_catalog_epoch.get('editor_session_id'),)" if session_only else "(_ub_catalog_epoch.get('editor_session_id'), _ub_catalog_epoch.get('registry_revision'))"
    return (
        "import json as _ub_catalog_json, unreal as _ub_catalog_unreal\n"
        "_ub_catalog_epoch = _ub_catalog_json.loads(_ub_catalog_unreal.UnrealBridgeUE58Library.get_official_toolset_catalog_snapshot_json(False))\n"
        f"if {fields} != {expected!r}:\n"
        "    raise RuntimeError('catalog_epoch_changed: refresh discovery before execution; original script was not run')\n"
        f"exec(compile({code!r}, '<unrealbridge-catalog>', 'exec'), globals(), globals())\n"
    )


def _resolve_manifest_function(library: str, function: str) -> "tuple[str, dict | None]":
    libraries = _current_catalog().manifest.get("libraries", {})
    # Discovery records expose the canonical reflected library name while the
    # compact grouped tools use names such as ``Editor`` and ``Blueprint``.
    # Accept both forms so a search result can be passed to ``bridge_call``
    # without a lossy name-conversion step.
    full_library = next(
        (name for name in libraries if name.lower() == library.lower()),
        None,
    )
    if full_library is not None:
        wrapper_class = full_library
        if wrapper_class.startswith("UnrealBridge"):
            wrapper_class = wrapper_class[len("UnrealBridge") :]
        if wrapper_class.endswith("Library"):
            wrapper_class = wrapper_class[: -len("Library")]
    else:
        wrapper_class = WRAPPER_CLASS_BY_GROUP.get(library.lower(), library)
        full_library = f"UnrealBridge{wrapper_class}Library"
    entry = (
        libraries
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


def _artifact_store_for(
    *,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
) -> ArtifactStore:
    configured = os.environ.get("UNREALBRIDGE_ARTIFACT_ROOT")
    if configured:
        return ArtifactStore(configured)
    binding = _CATALOG_CONTEXT.get()
    if binding is not None and binding.snapshot.session and binding.snapshot.key[0]:
        return ArtifactStore(Path(binding.snapshot.key[0]).parent / "Saved" / "UnrealBridge" / "Artifacts")
    try:
        args = _namespace(endpoint=endpoint, project=project, token=token, timeout=5.0)
        _host, _port, _resolved_token, project_path = bridge_cli.resolve_target(args)
        if project_path:
            candidate = Path(project_path)
            project_root = candidate.parent if candidate.suffix.lower() == ".uproject" else candidate
            return ArtifactStore(project_root / "Saved" / "UnrealBridge" / "Artifacts")
    except (SystemExit, OSError, RuntimeError, ValueError):
        pass
    local_app_data = os.environ.get("LOCALAPPDATA") or os.environ.get("TEMP") or SCRIPT_DIR
    return ArtifactStore(Path(local_app_data) / "UnrealBridge" / "Artifacts")


def _shape_response(
    value: Any,
    *,
    fields: Optional[List[str]] = None,
    omit: Optional[List[str]] = None,
    max_items: Optional[int] = None,
    cursor: Optional[str] = None,
    artifact_threshold_bytes: int = DEFAULT_RESULT_ARTIFACT_THRESHOLD,
    query_hash: Optional[str] = None,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
) -> Dict[str, Any]:
    shaped = shape_result(
        value,
        fields=fields,
        omit=omit,
        max_items=max_items,
        cursor=cursor,
        artifact_store=_artifact_store_for(endpoint=endpoint, project=project, token=token),
        artifact_threshold_bytes=artifact_threshold_bytes,
        query_hash=query_hash,
    )
    return {"success": True, **shaped}


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

    wrapped = bridge_cli._wrap_for_attr_enrichment(_catalog_guarded_script(code))
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
            if hasattr(value, "to_dict"):
                try:
                    converted = value.to_dict()
                    if isinstance(converted, dict):
                        return {{str(k): _ub_jsonable(v) for k, v in converted.items()}}
                except Exception:
                    pass
            if type(value).__name__ in {"Array", "Set"}:
                try:
                    return [_ub_jsonable(v) for v in value]
                except Exception:
                    pass
            if type(value).__name__ == "Map":
                try:
                    return {{str(k): _ub_jsonable(v) for k, v in value.items()}}
                except Exception:
                    pass
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


mcp = FastMCP(
    "unreal-bridge",
    instructions=(
        "Use grouped UnrealBridge operations for authenticated Unreal Editor edits. "
        "Operations and UE 5.8 official Toolsets are durable Jobs; poll bridge_get_job "
        "until terminal. Never save unrelated dirty packages."
    ),
)
# FastMCP 1.x does not expose a public server-version constructor argument.
# The adapter is intentionally pinned below MCP 2, so set the low-level field
# once to keep initialize/serverInfo aligned with the UnrealBridge release.
mcp._mcp_server.version = "3.1.0"


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


def _query_hash(payload: Dict[str, Any]) -> str:
    binding = _CATALOG_CONTEXT.get()
    if binding is not None:
        payload = {**payload, "catalog_fingerprint": binding.snapshot.fingerprint}
    return hashlib.sha256(
        json.dumps(payload, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
    ).hexdigest()


def _projection_options(
    fields: Optional[List[str]],
    omit: Optional[List[str]],
    result_options: Optional[Dict[str, Any]],
) -> "tuple[Optional[List[str]], Optional[List[str]]]":
    """Resolve MCP-safe projection names plus legacy `_fields`/`_omit` keys."""
    if result_options is None:
        return fields, omit
    if not isinstance(result_options, dict):
        raise ValueError("result_options must be an object")
    unknown = sorted(set(result_options) - {"_fields", "_omit", "fields", "omit"})
    if unknown:
        raise ValueError(f"unknown result_options keys: {unknown}")
    return (
        result_options.get("_fields", result_options.get("fields", fields)),
        result_options.get("_omit", result_options.get("omit", omit)),
    )


@mcp.tool()
@_with_catalog(for_execution=False)
def bridge_list_domains(
    provider: Optional[str] = None,
    max_items: int = 50,
    cursor: Optional[str] = None,
    fields: Optional[List[str]] = None,
    omit: Optional[List[str]] = None,
    result_options: Optional[Dict[str, Any]] = None,
    artifact_threshold_bytes: int = DEFAULT_RESULT_ARTIFACT_THRESHOLD,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
) -> Dict[str, Any]:
    """List capability domains; result_options accepts `_fields` and `_omit`."""
    query = {"kind": "domains", "provider": provider}
    try:
        fields, omit = _projection_options(fields, omit, result_options)
        return _shape_response(
            _current_catalog().tool_index.domains(provider=provider),
            fields=fields,
            omit=omit,
            max_items=max_items,
            cursor=cursor,
            artifact_threshold_bytes=artifact_threshold_bytes,
            query_hash=_query_hash(query),
        )
    except ValueError as exc:
        return {"success": False, "phase": "pagination", "retryable": False, "error": str(exc)}


@mcp.tool()
@_with_catalog(for_execution=False)
def bridge_search_tools(
    query: str,
    domain: Optional[str] = None,
    provider: Optional[str] = None,
    risk: Optional[str] = None,
    max_items: int = 50,
    cursor: Optional[str] = None,
    fields: Optional[List[str]] = None,
    omit: Optional[List[str]] = None,
    result_options: Optional[Dict[str, Any]] = None,
    artifact_threshold_bytes: int = DEFAULT_RESULT_ARTIFACT_THRESHOLD,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
) -> Dict[str, Any]:
    """Search native and official tools; result_options accepts `_fields`/`_omit`."""
    query_spec = {
        "kind": "search",
        "query": query,
        "domain": domain,
        "provider": provider,
        "risk": risk,
    }
    try:
        fields, omit = _projection_options(fields, omit, result_options)
        return _shape_response(
            _current_catalog().tool_index.search(query, domain=domain, provider=provider, risk=risk),
            fields=fields,
            omit=omit,
            max_items=max_items,
            cursor=cursor,
            artifact_threshold_bytes=artifact_threshold_bytes,
            query_hash=_query_hash(query_spec),
        )
    except ValueError as exc:
        return {"success": False, "phase": "pagination", "retryable": False, "error": str(exc)}


@mcp.tool()
@_with_catalog(for_execution=False)
def bridge_describe_tools(
    tool_ids: List[str],
    fields: Optional[List[str]] = None,
    omit: Optional[List[str]] = None,
    result_options: Optional[Dict[str, Any]] = None,
    artifact_threshold_bytes: int = DEFAULT_RESULT_ARTIFACT_THRESHOLD,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
) -> Dict[str, Any]:
    """Describe several tool ids returned by bridge_search_tools in one call."""
    if not tool_ids or len(tool_ids) > 100:
        return {
            "success": False,
            "phase": "schema",
            "retryable": False,
            "error": "tool_ids must contain between 1 and 100 ids",
        }
    records = _current_catalog().tool_index.describe(tool_ids)
    found_ids = {record["id"] for record in records}
    try:
        fields, omit = _projection_options(fields, omit, result_options)
    except ValueError as exc:
        return {"success": False, "phase": "schema", "retryable": False, "error": str(exc)}
    return _shape_response(
        {"tools": records, "missing": [tool_id for tool_id in tool_ids if tool_id not in found_ids]},
        fields=fields,
        omit=omit,
        max_items=100,
        artifact_threshold_bytes=artifact_threshold_bytes,
        query_hash=_query_hash({"kind": "describe", "ids": tool_ids}),
    )


@mcp.tool()
@_with_catalog(for_execution=False)
def bridge_list_domain_operations(
    domain: str,
    query: str = "",
    access: Optional[str] = None,
    max_items: int = 50,
    cursor: Optional[str] = None,
    fields: Optional[List[str]] = None,
    omit: Optional[List[str]] = None,
    result_options: Optional[Dict[str, Any]] = None,
    artifact_threshold_bytes: int = DEFAULT_RESULT_ARTIFACT_THRESHOLD,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
) -> Dict[str, Any]:
    """List executable, schema-audited official operations in one capability domain."""
    canonical = _current_catalog().domain_registry.canonical_domain(domain)
    if canonical not in _current_catalog().domain_registry.by_domain:
        return {
            "success": False,
            "phase": "domain",
            "retryable": False,
            "error": f"unknown official domain {domain!r}",
            "available_domains": [
                record["domain"] for record in _current_catalog().domain_registry.domains()
            ],
        }
    valid_access = {
        "readonly",
        "runtimeinteraction",
        "transactionalsync",
        "rejected",
    }
    if access and access.lower() not in valid_access:
        return {
            "success": False,
            "phase": "schema",
            "retryable": False,
            "error": (
                "access must be ReadOnly, RuntimeInteraction, "
                "TransactionalSync, or Rejected"
            ),
        }
    query_spec = {
        "kind": "domain-operations",
        "domain": canonical,
        "query": query,
        "access": access,
    }
    try:
        fields, omit = _projection_options(fields, omit, result_options)
        return _shape_response(
            {
                "domain": canonical,
                "operations": _current_catalog().domain_registry.list(
                    canonical, query=query, access=access
                ),
            },
            fields=fields,
            omit=omit,
            max_items=max_items,
            cursor=cursor,
            artifact_threshold_bytes=artifact_threshold_bytes,
            query_hash=_query_hash(query_spec),
        )
    except ValueError as exc:
        return {
            "success": False,
            "phase": "pagination",
            "retryable": False,
            "error": str(exc),
        }


@mcp.tool()
@_with_catalog(for_execution=False)
def bridge_describe_domain_operation(
    domain: str,
    operation: str,
    fields: Optional[List[str]] = None,
    omit: Optional[List[str]] = None,
    result_options: Optional[Dict[str, Any]] = None,
    artifact_threshold_bytes: int = DEFAULT_RESULT_ARTIFACT_THRESHOLD,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
) -> Dict[str, Any]:
    """Resolve one domain operation and return its exact input/output schemas."""
    try:
        record = _current_catalog().domain_registry.describe(domain, operation)
    except KeyError as exc:
        return {
            "success": False,
            "phase": "domain",
            "retryable": False,
            "error": str(exc),
        }
    try:
        fields, omit = _projection_options(fields, omit, result_options)
        return _shape_response(
            record,
            fields=fields,
            omit=omit,
            max_items=500,
            artifact_threshold_bytes=artifact_threshold_bytes,
            query_hash=_query_hash(
                {"kind": "domain-operation", "domain": domain, "operation": operation}
            ),
        )
    except ValueError as exc:
        return {"success": False, "phase": "schema", "retryable": False, "error": str(exc)}


def _context_source_index(project):
    binding = _CATALOG_CONTEXT.get()
    project_path = binding.project if binding is not None else project
    if not project_path:
        raise ContextError("An explicit project is required for source context")
    path = Path(project_path).resolve()
    if path.suffix.lower() != ".uproject" or not path.is_file():
        raise ContextError("Source context requires an existing .uproject path when the Editor is offline")
    key = project_identity(str(path))
    with _PROJECT_CONTEXT_LOCK:
        if key not in _PROJECT_CONTEXT_INDICES:
            if len(_PROJECT_CONTEXT_INDICES) >= 4:
                _PROJECT_CONTEXT_INDICES.popitem(last=False)
            _PROJECT_CONTEXT_INDICES[key] = ProjectContextIndex(path.parent)
        _PROJECT_CONTEXT_INDICES.move_to_end(key)
        return _PROJECT_CONTEXT_INDICES[key]


def _read_context_assets(paths, *, endpoint=None, project=None, token=None):
    binding = _CATALOG_CONTEXT.get()
    if binding is None or binding.snapshot.stale:
        return {"status": "editor_unavailable", "assets": []}
    code = textwrap.dedent(f"""
        import json, unreal
        _paths = {paths!r}
        def _dirty():
            return set(str(p.get_name()) for p in list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()) + list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()))
        _before = _dirty()
        _rows, _truncated = [], False
        for _path in _paths[:4]:
            _a = unreal.UnrealBridgeAssetLibrary.get_asset_info(_path)
            _row = {{'path': _path, 'found': bool(_a.found), 'class_path': str(_a.class_path), 'dirty': str(_a.package_name) in _before}}
            if _a.found:
                _deps = sorted(str(x) for x in unreal.UnrealBridgeAssetLibrary.get_package_dependencies(str(_a.package_name), False))
                _refs = sorted(str(x) for x in unreal.UnrealBridgeAssetLibrary.get_package_referencers(str(_a.package_name), False))
                _row.update({{'dependencies': _deps[:64], 'referencers': _refs[:64], 'dependency_count': len(_deps), 'referencer_count': len(_refs)}})
                _truncated = _truncated or len(_deps) > 64 or len(_refs) > 64
                if 'Blueprint' in str(_a.class_path):
                    _bp = unreal.UnrealBridgeBlueprintLibrary.get_blueprint_summary(_path)
                    if isinstance(_bp, tuple):
                        _bp = next((x for x in _bp if hasattr(x, 'parent_class_path')), None)
                    if _bp is not None:
                        _summary = {{k: str(getattr(_bp, k)) for k in ('name', 'path', 'parent_class_path', 'blueprint_type')}}
                        _summary.update({{k: int(getattr(_bp, k)) for k in ('variable_count', 'function_count', 'component_count', 'timeline_count', 'macro_count', 'total_node_count')}})
                        _row['blueprint_summary'] = _summary
            _rows.append(_row)
        _added = sorted(_dirty() - _before)
        print(json.dumps({{'status': 'live' if not _added else 'dirty_changed_during_read', 'assets': _rows, 'truncated': _truncated, 'dirty_added': _added}}))
    """)
    response = _execute_code(code, endpoint=endpoint, project=project, token=token, timeout=15.0, no_preflight=True)
    result = _last_json_output(response)
    if response.get("success") and isinstance(result, dict):
        return result
    return {"status": "asset_probe_failed", "assets": [],
            "error": str(response.get("error", "No asset probe response"))[-512:]}


_LOCAL_SEMANTIC_PROBE = textwrap.dedent("""
    import unreal
    from urllib.parse import urlsplit as _ub_semantic_urlsplit
    _ub_semantic_local = False
    try:
        _ub_semantic_class = unreal.load_class(None, '/Script/SemanticSearch.SemanticSearchSettings')
        _ub_semantic_settings = unreal.get_default_object(_ub_semantic_class)
        _ub_semantic_urls = [_ub_semantic_settings.get_editor_property(k) for k in ('EmbeddingBaseUrl', 'CaptioningBaseUrl')]
        _ub_semantic_local = all(_ub_semantic_urlsplit(str(u)).scheme in ('http', 'https') and _ub_semantic_urlsplit(str(u)).hostname in ('localhost', '127.0.0.1', '::1') for u in _ub_semantic_urls)
    except Exception:
        pass
""")


def _read_context_semantic(query, *, endpoint=None, project=None, token=None):
    binding = _CATALOG_CONTEXT.get()
    if binding is None or binding.snapshot.stale:
        return {"status": "editor_unavailable", "candidates": []}
    try:
        operation = binding.snapshot.domain_registry.resolve("semantic_search", "Search")
        if operation.access != "ReadOnly":
            return {"status": "semantic_tool_not_audited", "candidates": []}
    except KeyError:
        return {"status": "semantic_tool_unavailable", "candidates": []}
    probe = _execute_code(_LOCAL_SEMANTIC_PROBE + "\nimport json\nprint(json.dumps({'local_only': _ub_semantic_local}))",
                          endpoint=endpoint, project=project, token=token, timeout=10.0, no_preflight=True)
    if not probe.get("success") or not (_last_json_output(probe) or {}).get("local_only"):
        return {"status": "external_or_unknown_provider_disabled", "candidates": []}
    start, poll = _official_toolset_scripts(call_id=str(uuid.uuid4()), toolset=operation.toolset,
        tool=operation.tool, arguments={"query": query, "classFilter": [], "pathRegexes": [], "k": 10})
    # Recheck at dispatch, in the same GT slice as StartOfficialToolsetCall.
    start = _LOCAL_SEMANTIC_PROBE + "\nif not _ub_semantic_local:\n    raise RuntimeError('semantic_provider_is_not_local')\n" + start
    submitted = bridge_submit_job(start, poll_code=poll, poll_interval=0.1, run_timeout=30.0,
                                  queue_timeout=10.0, endpoint=endpoint, project=project, token=token, no_preflight=True)
    if not submitted.get("success"):
        return {"status": "semantic_submit_failed", "candidates": []}
    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        state = bridge_get_job(submitted["job_id"], endpoint=endpoint, project=project, token=token)
        if state.get("terminal"):
            payload = _last_json_output(state) or {}
            if state.get("job_state") != "succeeded" or not payload.get("success"):
                return {"status": "semantic_query_failed", "candidates": []}
            result = payload.get("result", {})
            candidates = result.get("returnValue", []) if isinstance(result, dict) else []
            if isinstance(candidates, str):
                try:
                    candidates = json.loads(candidates)
                except ValueError:
                    candidates = []
            # Captions are generated descriptions; keep paths and class identity only.
            safe = [{key: row[key] for key in ("path", "class") if key in row}
                    for row in candidates[:10] if isinstance(row, dict)] if isinstance(candidates, list) else []
            return {"status": "live_local_provider", "candidates": safe}
        time.sleep(0.1) # Host only; never sleep in an Editor execution slice.
    return {"status": "semantic_pending", "candidates": [], "job_id": submitted["job_id"]}


def _project_context_result(query, target_paths, *, mode, max_items, max_bytes, cursor, include_semantic,
                            endpoint, project, token):
    try:
        index = _context_source_index(project)
        binding = _CATALOG_CONTEXT.get()
        return build_context(index, query, target_paths, mode=mode, max_items=max_items, max_bytes=max_bytes,
            cursor=cursor, catalog_metadata=binding.snapshot.metadata(),
            asset_reader=lambda paths: _read_context_assets(paths, endpoint=endpoint, project=project, token=token),
            semantic_reader=(lambda text: _read_context_semantic(text, endpoint=endpoint, project=project, token=token)) if include_semantic else None)
    except (ContextError, OSError) as exc:
        return {"success": False, "phase": "project_context", "error": str(exc), "retryable": False}


@mcp.tool()
@_with_catalog(for_execution=False)
def bridge_project_context(query: str, target_paths: Optional[List[str]] = None,
    max_items: int = 20, max_bytes: int = 32768, cursor: Optional[str] = None,
    include_semantic: bool = False, endpoint: Optional[str] = None,
    project: Optional[str] = None, token: Optional[str] = None) -> Dict[str, Any]:
    """Return bounded local source lines/symbols and live package/BP references.

    target_paths accepts project source files/directories and Unreal asset paths.
    max_bytes bounds compact UTF-8 JSON including catalog metadata. File/asset
    changes invalidate cursors. Optional semantic search requires both configured
    model endpoints to be loopback; unavailable services explicitly degrade.
    No source upload, indexing, asset compilation or saving is performed.
    """
    return _project_context_result(query, target_paths, mode="context", max_items=max_items,
        max_bytes=max_bytes, cursor=cursor, include_semantic=include_semantic,
        endpoint=endpoint, project=project, token=token)


@mcp.tool()
@_with_catalog(for_execution=False)
def bridge_project_impact(target_paths: List[str], query: str = "", max_items: int = 30,
    max_bytes: int = 32768, cursor: Optional[str] = None, include_semantic: bool = False,
    endpoint: Optional[str] = None, project: Optional[str] = None,
    token: Optional[str] = None) -> Dict[str, Any]:
    """Find lexical source references and direct AssetRegistry referencers for targets.

    Results are evidence and impact candidates, not a complete compiler call graph.
    Shares bridge_project_context's scope, freshness, budget and local-only semantics.
    """
    return _project_context_result(query, target_paths, mode="impact", max_items=max_items,
        max_bytes=max_bytes, cursor=cursor, include_semantic=include_semantic,
        endpoint=endpoint, project=project, token=token)


_RUNTIME_RUNS = OrderedDict()
_RUNTIME_LOCK = threading.RLock()
_RUNTIME_SNAPSHOT_SCRIPT = '''import unreal, json
from unreal_bridge import Gameplay, Networking
def _rt_snapshot():
    c = json.loads(unreal.UnrealBridgeWorldLibrary.get_world_contexts(32))
    assert c.get('ok') and not c.get('truncated'), 'World catalog unavailable/truncated'
    lease = getattr(unreal, '_ubr_runtime_lease_v1', None) or {}
    c.update(pie=bool(unreal.UnrealBridgeEditorLibrary.is_in_pie()),
        dirty_content=sorted(p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()),
        dirty_maps=sorted(p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()),
        shader_jobs=int(unreal.UnrealBridgeEditorLibrary.get_shader_compile_job_count()),
        asset_jobs=int(unreal.UnrealBridgeEditorLibrary.get_asset_compile_job_count()),
        lease_owner=lease.get('run_id'), lease_pie_session_id=lease.get('pie_session_id'))
    return c
_rt_before = _rt_snapshot()
'''


@_with_catalog(for_execution=True)
def _runtime_rpc(op, arguments, run, cleanup, *, endpoint=None, project=None, token=None):
    if op == 'reconcile':
        outstanding = []
        for entry in run.report['jobs']:
            if entry.get('terminal') or not entry.get('job_id'):
                continue
            current = bridge_get_job(entry['job_id'], endpoint=endpoint, project=project, token=token, timeout=5)
            if not current.get('success') or not current.get('terminal'):
                outstanding.append(entry['job_id'])
            else:
                entry.update(terminal=True, job_state=current.get('job_state'))
        if outstanding:
            raise runtime_recipes.RuntimeFault('Known jobs still pending; cancellation request is not terminal confirmation')
    code = _RUNTIME_SNAPSHOT_SCRIPT
    code += f"_rt_expected_session = {run.session!r}\nassert _rt_expected_session is None or _rt_before['editor_session_id'] == _rt_expected_session, 'Editor session changed'\n"
    if op == 'v2_start':
        native_request = dict(schema='unrealbridge.network_session.v2',run_id=run.run_id,editor_session_id=run.session,**arguments)
        code += f"assert not getattr(unreal, '_ubr_runtime_lease_v1', None), 'Unresolved v1 lease'\n_rt_network=json.loads(unreal.UnrealBridgeNetworkSessionLibrary.start_network_session({json.dumps(native_request,sort_keys=True,separators=(',',':'))!r}))\n"
    elif op == 'v2_state':
        code += f"_rt_network=json.loads(unreal.UnrealBridgeNetworkSessionLibrary.get_network_session_state({run.run_id!r}))\n"
    elif op in ('v2_join','v2_stop'):
        request_id=run.run_id+':'+str(len(run.report['jobs'])+1)
        if op=='v2_join':
            code += f"_rt_network=json.loads(unreal.UnrealBridgeNetworkSessionLibrary.join_network_client({run.run_id!r},{run.pie_session!r},{request_id.replace(':','-')!r},{arguments['remote_client_ordinal']!r}))\n"
        else:
            code += f"_rt_network=json.loads(unreal.UnrealBridgeNetworkSessionLibrary.stop_owned_network_session({run.run_id!r},{run.pie_session!r},{request_id.replace(':','-')!r}))\n"
    elif op == 'start':
        code += f'''assert not _rt_before['pie'] and not _rt_before.get('pie_session_id'), 'Existing PIE is not owned'
assert not _rt_before['dirty_content'] and not _rt_before['dirty_maps'], 'Dirty baseline changed'
assert not getattr(unreal, '_ubr_runtime_lease_v1', None), 'An unresolved runtime lease already exists'
_rt_lease = {{'run_id': {run.run_id!r}, 'editor_session_id': _rt_before['editor_session_id'], 'pie_session_id': '', 'phase': 'starting'}}
unreal._ubr_runtime_lease_v1 = _rt_lease
_rt_ok = bool(unreal.UnrealBridgeEditorLibrary.start_network_pie({arguments['client_count']!r}, True))
_rt_after = _rt_snapshot()
_rt_lease['pie_session_id'] = _rt_after.get('pie_session_id')
_rt_lease['phase'] = 'started' if _rt_ok else 'start_failed'
assert _rt_ok, 'Native PIE start rejected'
'''
    elif op in ('stop', 'input'):
        if getattr(run,'is_v2',False) and op=='input':
            code += f"_rt_owned=json.loads(unreal.UnrealBridgeNetworkSessionLibrary.get_network_session_state({run.run_id!r}))\nassert _rt_owned.get('ok') and _rt_owned.get('owned_pie_session_id')=={run.pie_session!r} and _rt_owned.get('topology_ready'), 'Native PIE lease mismatch'\n"
        else:
            code += f"assert _rt_before['lease_owner'] == {run.run_id!r} and _rt_before['pie_session_id'] == {run.pie_session!r} and _rt_before['lease_pie_session_id'] == {run.pie_session!r}, 'PIE lease mismatch'\n"
        if op == 'stop':
            code += "assert unreal.UnrealBridgeEditorLibrary.stop_pie(), 'PIE stop rejected'\n"
        else:
            code += f"assert Gameplay.inject_enhanced_input_axis(input_action_path={arguments['input_action_path']!r}, axis_value=unreal.Vector(*{arguments['value']!r})), 'Semantic input rejected'\n"
    elif op == 'release':
        code += f"assert _rt_before['lease_owner'] == {run.run_id!r} and not _rt_before['pie'] and not _rt_before['pie_session_id'], 'Lease release refused'\nunreal._ubr_runtime_lease_v1 = None\n"
    elif op not in ('snapshot', 'observe', 'diagnostics', 'reconcile'):
        raise runtime_recipes.RuntimeFault('Unsupported runtime RPC')
    code += "_rt_result = _rt_snapshot()\n"
    if op.startswith('v2_'):
        code += "_rt_result['network_session']=_rt_network\n"
    if op in ('observe', 'input'):
        code += f"_rt_world = next(w for w in _rt_result['worlds'] if w['world_handle'] == {arguments['world_handle']!r})\n"
        code += f"assert _rt_result['pie_session_id'] == {run.pie_session!r}, 'PIE identity changed'\n"
    if op == 'observe':
        code += """_rt_net = next((w for w in Networking.get_network_world_snapshots(replicated_actors_only=False, max_actors_per_world=1) if w.world == _rt_world['world_path']), None)
_rt_result.update(has_begun_play=_rt_world['has_begun_play'], controller_available=Gameplay.get_control_rotation() is not None,
    pawn_available=bool(Gameplay.get_player_pawn_actor_name()), actor_count=_rt_net.total_actor_count if _rt_net else None,
    observed_world=_rt_world['world_handle'])
"""
    if op == 'diagnostics':
        code += "_rt_result['log_excerpt'] = [str(v)[:600] for v in unreal.UnrealBridgeEditorLibrary.get_recent_log_lines(20, 'Warning')]\n"
    code += "print(json.dumps(_rt_result, ensure_ascii=False))\n"
    if len(run.report['jobs']) >= 512 and not cleanup:
        raise runtime_recipes.RuntimeFault('Runtime job evidence budget reached')
    entry = {'operation': op, 'input_hash': runtime_recipes.digest(arguments), 'script_hash': hashlib.sha256(code.encode('utf-8')).hexdigest(),
        'editor_session_id': run.session, 'world_handle': arguments.get('world_handle'), 'terminal': False, 'dispatch_state': 'intent_recorded'}
    run.report['jobs'].append(entry)
    run.persist()
    is_mutation = op in ('start', 'input', 'stop', 'release', 'v2_start', 'v2_join', 'v2_stop')
    try:
        # Idempotency identifies this dispatch only. A timed-out mutation is never
        # resubmitted; known jobs are queried and unknown dispatches are reported.
        submitted = bridge_submit_job(code=code, idempotency_key=run.run_id + ':' + str(len(run.report['jobs'])),
            run_timeout=20, queue_timeout=10, timeout=5, world_handle=arguments.get('world_handle'),
            endpoint=endpoint, project=project, token=token)
        if not submitted.get('success') or not submitted.get('job_id'):
            if is_mutation and submitted.get('phase') != 'preflight': run.unknown = True
            entry.update(dispatch_state='rejected_before_dispatch' if submitted.get('phase') == 'preflight' else 'unconfirmed', error=submitted.get('error'))
            raise runtime_recipes.RuntimeFault('Runtime job submission not confirmed: ' + str(submitted.get('error', 'unknown'))[:1000])
        entry.update(job_id=submitted['job_id'], dispatch_state='accepted')
        run.persist()
        deadline = time.monotonic() + 25 if cleanup else min(run.deadline, time.monotonic() + 25)
        while True:
            current = bridge_get_job(entry['job_id'], endpoint=endpoint, project=project, token=token, timeout=5)
            if not current.get('success'):
                raise runtime_recipes.RuntimeFault('Runtime job transport unavailable; outcome unconfirmed')
            if current.get('terminal'):
                entry.update(terminal=True, job_state=current.get('job_state'), dispatch_state='terminal')
                run.persist()
                if current.get('job_state') != 'succeeded':
                    raise runtime_recipes.RuntimeFault('Runtime job ended ' + str(current.get('job_state')) + ': ' + str(current.get('error') or current.get('job_result', {}).get('error', ''))[:1000])
                payload = _last_json_output(current)
                if not payload or not payload.get('ok'):
                    raise runtime_recipes.RuntimeFault('Runtime job lacks a valid evidence envelope')
                return payload
            if time.monotonic() >= deadline or (not cleanup and run.cancelled.is_set()):
                entry['cancel_requested'] = True
                entry['cancel_response'] = bridge_cancel_job(entry['job_id'], endpoint=endpoint, project=project, token=token, timeout=5).get('success')
                run.persist()
                raise runtime_recipes.RuntimeFault('Job wait ended; cancellation was requested, terminal state still requires reconciliation')
            time.sleep(.1)
    except Exception:
        if is_mutation and not entry.get('job_id') and entry['dispatch_state'] != 'rejected_before_dispatch':
            run.unknown = True
        raise
    finally:
        run.persist()


@mcp.tool()
@_with_catalog(for_execution=True)
def bridge_runtime_run(recipe: Dict[str, Any], endpoint: Optional[str] = None,
    project: Optional[str] = None, token: Optional[str] = None) -> Dict[str, Any]:
    """Run a typed observe/owned_pie acceptance recipe as a persisted Scenario.

    v1 supports 1/3 worlds. Explicit v2 supports listen/dedicated topology,
    bounded late joins and declared network emulation, with native ownership.
    Scoped EnhancedInput and exact cleanup/retain-for-feedback policies. No Content
    writes, saves, desktop input, automatic replay or takeover of existing PIE.
    Poll bridge_runtime_status; cancellation is a request until cleanup verifies.
    """
    try:
        is_v2 = isinstance(recipe,dict) and recipe.get('schema')==network_sessions.SCHEMA
        spec = network_sessions.normalize(recipe) if is_v2 else runtime_recipes.normalize(recipe)
        store = _artifact_store_for(endpoint=endpoint, project=project, token=token)
        root = store.root / 'runtime'
        with _RUNTIME_LOCK:
            active = sum(bool(manager._threads) for run, manager, key in _RUNTIME_RUNS.values())
            if active >= 4:
                raise ValueError('At most four runtime recipes may execute in this host')
            run_type = network_sessions.NetworkSessionRun if is_v2 else runtime_recipes.RuntimeRun
            run = run_type(recipe if is_v2 else spec, lambda op, args, owner, cleanup: _runtime_rpc(op, args, owner, cleanup,
                endpoint=endpoint, project=project, token=token), root / 'reports')

            def cleanup(state):
                result = run.cleanup(state)
                record = store.put(run.report, kind='runtime_acceptance_report')
                return {**result, 'artifact': record.as_dict(), 'report_path': str(run.path)}

            manager = ScenarioManager(root / 'scenarios', lambda step, timeout: run.execute(), cleanup=cleanup)
            state = manager.submit({'name': spec.get('name') or 'Runtime acceptance', 'runtime_run_id': run.run_id,
                'steps': [{'id': 'runtime', 'type': 'runtime_recipe', 'risk': 'RuntimeInteraction' if spec['mode'] == 'owned_pie' else 'ReadOnly',
                    'retry': 0, 'timeout': spec['timeout_seconds'], 'input_hash': run.report['input_hash']}]})
            _RUNTIME_RUNS[state['scenario_id']] = (run, manager, str(root))
            while len(_RUNTIME_RUNS) > 32:
                old = next((k for k, (_, m, _) in _RUNTIME_RUNS.items() if not m._threads), None)
                if old is None: break
                del _RUNTIME_RUNS[old]
        return {'success': True, 'scenario_id': state['scenario_id'], 'run_id': run.run_id, 'status': state['status'],
            'report_path': str(run.path), 'state_path': str(manager._path(state['scenario_id']))}
    except (ValueError, OSError) as exc:
        return {'success': False, 'phase': 'runtime_recipe', 'error': str(exc)}


@mcp.tool()
@_with_catalog(for_execution=True)
def bridge_control_network_session(operation: str, run_id: str, expected_pie_session: str = '',
    request_id: str = '', remote_client_ordinal: int = 0, endpoint: Optional[str] = None,
    project: Optional[str] = None, token: Optional[str] = None) -> Dict[str, Any]:
    """Read, join or stop a v2 PIE retained for feedback. Requires exact native ownership.

    A persisted host record never authorizes takeover; the native run/session nonce
    must still match. Stop is a request; poll state until cleaned before claiming it.
    """
    try:
        if operation not in ('state','join','stop') or not isinstance(run_id,str) or not re.fullmatch(r'[A-Za-z0-9_.-]{1,128}',run_id):
            raise ValueError('Typed operation and run_id required')
        if operation!='state' and (not re.fullmatch(r'[A-Za-z0-9_.-]{1,128}',request_id) or not re.fullmatch(r'[A-Fa-f0-9]{32}',expected_pie_session)):
            raise ValueError('Exact PIE session nonce and request_id required')
        if operation=='join' and (type(remote_client_ordinal) is not int or not 2<=remote_client_ordinal<=4):
            raise ValueError('The next remote client ordinal must be 2..4')
        if operation=='state': code=f"unreal.UnrealBridgeNetworkSessionLibrary.get_network_session_state({run_id!r})"
        elif operation=='join': code=f"unreal.UnrealBridgeNetworkSessionLibrary.join_network_client({run_id!r},{expected_pie_session!r},{request_id!r},{remote_client_ordinal!r})"
        else: code=f"unreal.UnrealBridgeNetworkSessionLibrary.stop_owned_network_session({run_id!r},{expected_pie_session!r},{request_id!r})"
        return bridge_submit_job('import unreal\nprint('+code+')',
            idempotency_key=None if operation=='state' else 'network-control:'+run_id+':'+request_id,
            run_timeout=30,endpoint=endpoint,project=project,token=token)
    except ValueError as exc:
        return {'success':False,'phase':'network_session','error':str(exc)}


@mcp.tool()
@_with_catalog(for_execution=False)
def bridge_runtime_status(scenario_id: str, endpoint: Optional[str] = None,
    project: Optional[str] = None, token: Optional[str] = None) -> Dict[str, Any]:
    """Read persisted runtime status/evidence; absent executors need reconciliation."""
    try:
        root = _artifact_store_for(endpoint=endpoint, project=project, token=token).root / 'runtime'
        with _RUNTIME_LOCK:
            found = _RUNTIME_RUNS.get(scenario_id)
        if found and found[2] != str(root):
            raise ValueError('Runtime scenario belongs to another project/artifact root')
        manager = found[1] if found else ScenarioManager(root / 'scenarios', lambda *a: None)
        state = manager.get(scenario_id)
        active = bool(found and scenario_id in manager._threads and manager._threads[scenario_id].is_alive())
        result = runtime_recipes.recovery_view(state, active)
        # Detailed job/step evidence is retained in the report and artifact.
        return {'success': True, 'scenario_id': scenario_id, 'status': result['status'], 'error': result.get('error'),
            'recovery': result.get('recovery'), 'cleanup': result.get('cleanup'),
            'run_id': state['spec'].get('runtime_run_id'),
            'report_path': str(root / 'reports' / (state['spec']['runtime_run_id'] + '.json'))}
    except (ValueError, OSError, KeyError) as exc:
        return {'success': False, 'phase': 'runtime_status', 'error': str(exc)}


@mcp.tool()
def bridge_external_session(request: Dict[str, Any]) -> Dict[str, Any]:
    """Start/control a bounded independent UnrealEditor -game/-server session.

    Requires explicit .uproject, run_id, typed operation and request_id. Local
    authenticated project observer, PID/creation/exe/parent verification, no shell,
    no forced termination. Mutations return a durable operation_id; use operation
    status to inspect completion. This is uncooked editor_game, not packaged DS.
    Does not depend on an Editor Bridge being connected or access another process's UObjects.
    """
    try: return external_mcp.dispatch(request)
    except (OSError,ValueError,KeyError,TypeError) as exc:
        return {'success':False,'phase':'external_session','error':str(exc)}


@mcp.tool()
@_with_catalog(for_execution=True)
def bridge_submit_pointer_action(request: Dict[str,Any], endpoint: Optional[str]=None,
    project: Optional[str]=None, token: Optional[str]=None) -> Dict[str,Any]:
    """Queue a native pointer v1 sequence against fresh widget geometry.

    Get geometry with SlateInput.GetWidgetInputGeometry, then supply its World,
    local player, window, widget handle, generation and revision. Maximum 120
    move/down/up events and 10 seconds. Native Slate ticks perform hit tests and
    route only to that window; stale/occluded/foreign capture is rejected. Poll
    SlateInput.GetPointerSequenceState; cancellation releases only owned capture.
    """
    try: return pointer_input.submit(SimpleNamespace(bridge_submit_job=bridge_submit_job),request,endpoint=endpoint,project=project,token=token)
    except (ValueError,TypeError) as exc: return {'success':False,'phase':'pointer_contract','error':str(exc)}


@mcp.tool()
@_with_catalog(for_execution=True)
def bridge_submit_audio_mix_session(request: Dict[str,Any], endpoint: Optional[str]=None,
    project: Optional[str]=None, token: Optional[str]=None) -> Dict[str,Any]:
    """Begin a native audio session bound to the exact World and AudioDevice.

    Get Audio.GetAudioMixContext first. Typed sources use centimetres, linear/dB
    gain and pitch ratios; a 5..300 second lease owns transient components/mixes.
    Poll Audio.GetAudioMixSession for actual playback/envelope/mix evidence and
    use Audio.EndAudioMixSession to remove only this session's effects.
    """
    try: return audio_sessions.submit(SimpleNamespace(bridge_submit_job=bridge_submit_job),request,endpoint=endpoint,project=project,token=token)
    except upgrade_contract.UpgradeFault as exc: return {'success':False,**exc.result()}
    except (ValueError,TypeError) as exc: return {'success':False,'phase':'audio_contract','error':str(exc)}


@mcp.tool()
@_with_catalog(for_execution=True)
def bridge_submit_authoring_request(request: Dict[str,Any], endpoint: Optional[str]=None,
    project: Optional[str]=None, token: Optional[str]=None) -> Dict[str,Any]:
    """Preview/apply bounded typed authoring with upgrade v1 project/session/revision context.

    One explicit target, or up to 16 for audio routing parent/child consistency;
    no deletion, arbitrary property paths or scripts.
    A native guarded ChangeSet retains failures unsaved. Saving requires explicit
    save_policy=declared_targets and dry_run=false. Poll the returned durable Job;
    an uncertain response never authorizes replay with a fresh request ID.
    """
    try: return authoring.submit(SimpleNamespace(bridge_submit_job=bridge_submit_job),request,endpoint=endpoint,project=project,token=token)
    except upgrade_contract.UpgradeFault as exc: return {'success':False,**exc.result()}
    except (ValueError,TypeError) as exc: return {'success':False,'phase':'authoring_contract','error':str(exc)}


@mcp.tool()
@_with_catalog(for_execution=False)
def bridge_runtime_cancel(scenario_id: str, endpoint: Optional[str] = None,
    project: Optional[str] = None, token: Optional[str] = None) -> Dict[str, Any]:
    """Request cancellation in this host. Does not claim job/PIE completion."""
    status = bridge_runtime_status(scenario_id, endpoint=endpoint, project=project, token=token)
    if not status.get('success') or status.get('status') not in runtime_recipes.ACTIVE:
        return status
    with _RUNTIME_LOCK:
        found = _RUNTIME_RUNS.get(scenario_id)
    if not found:
        return {**status, 'status': 'needs_reconciliation', 'cancel_requested': False}
    run, manager, _ = found
    run.cancelled.set()
    manager.cancel(scenario_id)
    return {'success': True, 'scenario_id': scenario_id, 'cancel_requested': True, 'terminal': False,
            'message': 'Wait for runtime_status cleanup and side_effects_resolved before treating cancellation as complete'}


def _read_graph_snapshot(blueprint_path, graph_name, *, endpoint, project, token):
    if not isinstance(blueprint_path, str) or not blueprint_path.startswith('/') or not graph_name or len(graph_name) > 256:
        raise graph_codec.GraphError("Explicit asset path and graph name are required")
    code = f'''import unreal, json
_dirty = lambda: sorted(str(p.get_name()) for p in list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()) + list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()))
_before = _dirty()
_snapshot = unreal.UnrealBridgeBlueprintLibrary.snapshot_graph_json({blueprint_path!r}, {graph_name!r})
_after = _dirty()
print(json.dumps({{"snapshot": _snapshot, "dirty_before": _before, "dirty_after": _after}}))
'''
    result = _execute_code(code, endpoint=endpoint, project=project, token=token, timeout=30)
    payload = _last_json_output(result)
    if not result.get('success') or not isinstance(payload, dict) or not payload.get('snapshot'):
        raise graph_codec.GraphError("Native snapshot unavailable: missing graph, unsupported build, or native size limit")
    if payload['dirty_before'] != payload['dirty_after']:
        raise graph_codec.GraphError("Dirty state changed during snapshot; inspect Editor before continuing")
    try:
        snapshot = json.loads(payload['snapshot'])
    except ValueError as exc:
        raise graph_codec.GraphError("Invalid native snapshot") from exc
    graph_codec.validate(snapshot)
    return snapshot, payload['dirty_after']


@mcp.tool()
@_with_catalog(for_execution=True)
def bridge_graph_export(blueprint_path: str, graph_name: str, include_layout: bool = True,
    node_ids: Optional[List[str]] = None, hops: int = 0, max_bytes: int = 131072,
    endpoint: Optional[str] = None, project: Optional[str] = None, token: Optional[str] = None) -> Dict[str, Any]:
    """Read a lossless supported-field graph table; optional slices retain boundary edges.

    Measures compact JSON bytes, not tokens. Refuses missing types/defaults and
    output overflow. Unknown reflected node fields are preserved. Does not save.
    """
    try:
        if type(max_bytes) is not int or not 4096 <= max_bytes <= 262144:
            raise graph_codec.GraphError('max_bytes must be 4096..262144')
        raw, dirty = _read_graph_snapshot(blueprint_path, graph_name, endpoint=endpoint, project=project, token=token)
        compact = graph_codec.encode(raw, include_layout=include_layout, node_ids=node_ids, hops=hops)
        result = {'success': True, 'blueprint_path': blueprint_path, 'graph': compact, 'dirty_packages': dirty,
            'measurements': {'raw_json_bytes': len(graph_codec.encoded(raw)), 'compact_json_bytes': len(graph_codec.encoded(compact)),
                'basis': 'compact UTF-8 JSON; optional slice/layout changes affect comparability'},
            'catalog_metadata': _CATALOG_CONTEXT.get().snapshot.metadata()}
        if len(graph_codec.encoded(result)) > max_bytes:
            return {'success': False, 'phase': 'graph_budget', 'error': 'Output exceeds budget; request a node slice or larger budget',
                    'required_bytes': len(graph_codec.encoded(result)), 'source_revision': compact['source_revision']}
        return result
    except graph_codec.GraphError as exc:
        return {'success': False, 'phase': 'graph_export', 'error': str(exc)}


@mcp.tool()
def bridge_graph_decode(graph: Dict[str, Any]) -> Dict[str, Any]:
    """Decode supported compact fields and verify content revision; no Editor access."""
    try:
        return {'success': True, 'snapshot': graph_codec.decode(graph)}
    except graph_codec.GraphError as exc:
        return {'success': False, 'phase': 'graph_decode', 'error': str(exc)}


@mcp.tool()
def bridge_graph_diff(before: Dict[str, Any], after: Dict[str, Any]) -> Dict[str, Any]:
    """Compare node/pin/default/extension changes; slice diffs are explicitly incomplete."""
    try:
        return {'success': True, **graph_codec.diff(before, after)}
    except graph_codec.GraphError as exc:
        return {'success': False, 'phase': 'graph_diff', 'error': str(exc)}


@mcp.tool()
@_with_catalog(for_execution=True)
def bridge_graph_dry_run(blueprint_path: str, graph_name: str, expected_revision: str,
    operations: List[Dict[str, Any]], endpoint: Optional[str] = None,
    project: Optional[str] = None, token: Optional[str] = None) -> Dict[str, Any]:
    """Re-read full graph and validate exact revision/typed existing-pin proposals.

    Pin arguments are IDs, not names. Maps conservative operations to legacy
    ApplyGraphOps without executing them. Native schema validation is still
    required before any later authorized write; ApplyGraphOps is not atomic.
    """
    try:
        raw, dirty = _read_graph_snapshot(blueprint_path, graph_name, endpoint=endpoint, project=project, token=token)
        result = graph_codec.dry_run(graph_codec.encode(raw), expected_revision, operations)
        return {**result, 'blueprint_path': blueprint_path, 'dirty_packages': dirty}
    except graph_codec.GraphError as exc:
        return {'success': False, 'phase': 'graph_dry_run', 'error': str(exc), 'executed': False}


@mcp.tool()
def bridge_read_artifact(
    artifact_id: str,
    offset: int = 0,
    max_bytes: int = 64 * 1024,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
) -> Dict[str, Any]:
    """Read one bounded chunk of a large-result, screenshot, log, trace, or scenario artifact."""
    try:
        return {
            "success": True,
            **_artifact_store_for(endpoint=endpoint, project=project, token=token).read(
                artifact_id, offset=offset, max_bytes=max_bytes
            ),
        }
    except (ValueError, FileNotFoundError, OSError) as exc:
        return {"success": False, "phase": "artifact", "retryable": False, "error": str(exc)}


@mcp.tool()
def bridge_store_file_artifact(
    path: str,
    kind: str = "file",
    media_type: str = "application/octet-stream",
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
) -> Dict[str, Any]:
    """Store a screenshot, log, trace, or report file as a content-addressed artifact."""
    try:
        source = Path(path).resolve(strict=True)
        if not source.is_file():
            raise ValueError("path must identify a file")
        size = source.stat().st_size
        if size > 256 * 1024 * 1024:
            raise ValueError("artifact file exceeds the 256 MiB ingestion limit")
        record = _artifact_store_for(
            endpoint=endpoint, project=project, token=token
        ).put(source.read_bytes(), kind=kind, media_type=media_type)
        return {"success": True, "source": str(source), **record.as_dict()}
    except (ValueError, OSError) as exc:
        return {
            "success": False,
            "phase": "artifact",
            "retryable": False,
            "error": str(exc),
        }


@mcp.tool()
def bridge_compare_golden_image(
    expected_path: str,
    actual_path: str,
    pixel_delta: int = 8,
    max_mae: float = 0.01,
    max_changed_ratio: float = 0.01,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
) -> Dict[str, Any]:
    """Compare screenshots, return metrics, and retain JSON plus PNG diff artifacts."""
    if compare_golden_images is None:
        return {
            "success": False,
            "phase": "golden-image",
            "retryable": False,
            "error": (
                "golden-image dependencies are unavailable; launch the MCP server "
                "with scripts/mcp-requirements.txt: " + GOLDEN_IMAGE_IMPORT_ERROR
            ),
        }
    try:
        result, diff_png = compare_golden_images(
            Path(expected_path),
            Path(actual_path),
            pixel_delta=pixel_delta,
            max_mae=max_mae,
            max_changed_ratio=max_changed_ratio,
            make_diff=True,
        )
        store = _artifact_store_for(
            endpoint=endpoint, project=project, token=token
        )
        report = store.put(result, kind="golden-image-report")
        response: Dict[str, Any] = {
            "success": True,
            **result,
            "report_artifact": report.as_dict(),
        }
        if diff_png is not None:
            response["diff_artifact"] = store.put(
                diff_png, kind="golden-image-diff", media_type="image/png"
            ).as_dict()
        return response
    except (OSError, ValueError) as exc:
        return {
            "success": False,
            "phase": "golden-image",
            "retryable": False,
            "error": str(exc),
        }


def _world_scoped_script(code: str, world_handle: Optional[str]) -> str:
    if world_handle is None:
        return code
    if not isinstance(world_handle, str) or not world_handle or len(world_handle) > 256:
        raise ValueError("world_handle must be a nonempty string of at most 256 characters")
    return (
        "from unreal_bridge_world_context import world_scope as _ub_world_scope\n"
        f"with _ub_world_scope({world_handle!r}):\n"
        f"    exec(compile({code!r}, '<UnrealBridgeWorldJob>', 'exec'), globals(), globals())\n"
    )


@mcp.tool()
def bridge_submit_job(
    code: str,
    idempotency_key: Optional[str] = None,
    queue_timeout: float = 300.0,
    poll_code: Optional[str] = None,
    poll_interval: float = 0.25,
    run_timeout: float = 300.0,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
    timeout: float = 15.0,
    no_preflight: bool = False,
    world_handle: Optional[str] = None,
) -> Dict[str, Any]:
    """Submit a durable Job; optional world_handle scopes every start/poll slice."""
    if not no_preflight:
        errors, warnings = bridge_cli._preflight_or_skip(code)
        if errors:
            return {"success": False, "error": "\n".join(errors), "warnings": warnings, "phase": "preflight"}
    else:
        warnings = []
    try:
        code = _world_scoped_script(_catalog_guarded_script(code), world_handle)
        if poll_code:
            poll_code = _world_scoped_script(_catalog_guarded_script(poll_code, session_only=True), world_handle)
    except ValueError as exc:
        return {"success": False, "error": str(exc), "phase": "preflight"}
    payload: Dict[str, Any] = {
        "id": str(uuid.uuid4()),
        "command": "submit_job",
        "script": bridge_cli._wrap_for_attr_enrichment(code),
        "queue_timeout": queue_timeout,
        "poll_interval": poll_interval,
        "run_timeout": run_timeout,
        **bridge_cli.client_handshake(),
    }
    if poll_code:
        payload["poll_script"] = bridge_cli._wrap_for_attr_enrichment(poll_code)
    if idempotency_key:
        payload["idempotency_key"] = idempotency_key
    result = _send_command(
        payload, endpoint=endpoint, project=project, token=token, timeout=timeout,
    )
    if warnings:
        result = dict(result)
        result["warnings"] = warnings
    return result


def _official_toolset_scripts(
    *,
    call_id: str,
    toolset: str,
    tool: str,
    arguments: Dict[str, Any],
    access: str = "ReadOnly",
) -> tuple[str, str]:
    payload = {
        "call_id": call_id,
        "toolset": toolset,
        "tool": tool,
        "arguments": arguments,
        "access": access,
    }
    encoded = base64.b64encode(
        json.dumps(payload, ensure_ascii=False, sort_keys=True).encode("utf-8")
    ).decode("ascii")
    start = textwrap.dedent(
        f"""
        import base64
        import json
        import unreal
        _p = json.loads(base64.b64decode({encoded!r}).decode("utf-8"))
        if _p["access"] == "RuntimeInteraction":
            _started = json.loads(
                unreal.UnrealBridgeUE58Library.start_official_runtime_toolset_call(
                    _p["call_id"], _p["toolset"], _p["tool"],
                    json.dumps(_p["arguments"], ensure_ascii=False), True))
        else:
            _started = json.loads(
                unreal.UnrealBridgeUE58Library.start_official_toolset_call(
                    _p["call_id"], _p["toolset"], _p["tool"],
                    json.dumps(_p["arguments"], ensure_ascii=False)))
        if not _started.get("success"):
            raise RuntimeError(_started.get("error", "official ToolsetRegistry call failed to start"))
        print(json.dumps(_started, ensure_ascii=False))
        """
    ).strip()
    poll = textwrap.dedent(
        f"""
        import json
        import unreal
        _poll = json.loads(
            unreal.UnrealBridgeUE58Library.poll_official_toolset_call({call_id!r}))
        print(json.dumps(_poll, ensure_ascii=False))
        """
    ).strip()
    return start, poll


def _last_json_output(response: Dict[str, Any]) -> Optional[Dict[str, Any]]:
    return last_json_object(response)


@mcp.tool()
@_with_catalog(for_execution=False)
def bridge_list_official_toolsets(
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
    timeout: float = 15.0,
) -> Dict[str, Any]:
    """Submit a read-only Job that returns the UE 5.8 ToolsetRegistry catalog."""
    return bridge_submit_job(
        "import unreal\nprint(unreal.UnrealBridgeUE58Library.get_official_toolset_catalog_json())",
        endpoint=endpoint,
        project=project,
        token=token,
        timeout=timeout,
        no_preflight=True,
    )


@mcp.tool()
@_with_catalog(for_execution=False)
def bridge_describe_official_toolset(
    toolset: str,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
    timeout: float = 15.0,
) -> Dict[str, Any]:
    """Submit a read-only Job that returns one UE 5.8 official toolset schema."""
    encoded = base64.b64encode(toolset.encode("utf-8")).decode("ascii")
    code = (
        "import base64, unreal\n"
        f"_name=base64.b64decode({encoded!r}).decode('utf-8')\n"
        "print(unreal.UnrealBridgeUE58Library.get_official_toolset_schema_json(_name))"
    )
    return bridge_submit_job(
        code,
        endpoint=endpoint,
        project=project,
        token=token,
        timeout=timeout,
        no_preflight=True,
    )


@mcp.tool()
@_with_catalog()
def bridge_submit_official_toolset_job(
    toolset: str,
    tool: str,
    arguments: Optional[Dict[str, Any]] = None,
    idempotency_key: Optional[str] = None,
    queue_timeout: float = 300.0,
    poll_interval: float = 0.25,
    run_timeout: float = 300.0,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
    timeout: float = 15.0,
) -> Dict[str, Any]:
    """Run one schema-declared read-only UE 5.8 tool as a recoverable polling Job."""
    stable_payload = json.dumps(
        {"toolset": toolset, "tool": tool, "arguments": arguments or {}},
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    )
    call_seed = idempotency_key or str(uuid.uuid4())
    call_id = "official-" + hashlib.sha256(
        f"{call_seed}\n{stable_payload}".encode("utf-8")
    ).hexdigest()[:32]
    start, poll = _official_toolset_scripts(
        call_id=call_id,
        toolset=toolset,
        tool=tool,
        arguments=arguments or {},
    )
    result = bridge_submit_job(
        start,
        idempotency_key=idempotency_key,
        queue_timeout=queue_timeout,
        poll_code=poll,
        poll_interval=poll_interval,
        run_timeout=run_timeout,
        endpoint=endpoint,
        project=project,
        token=token,
        timeout=timeout,
        no_preflight=True,
    )
    if result.get("success"):
        result = dict(result)
        result.update(
            provider="EpicToolsetRegistry",
            toolset=toolset,
            tool=tool,
            risk="ReadOnly",
            execution="PollingJob",
            save_behavior="Never",
        )
    return result


@mcp.tool()
@_with_catalog()
def bridge_submit_official_runtime_job(
    toolset: str,
    tool: str,
    arguments: Optional[Dict[str, Any]] = None,
    allow_runtime_side_effects: bool = False,
    idempotency_key: Optional[str] = None,
    queue_timeout: float = 300.0,
    poll_interval: float = 0.25,
    run_timeout: float = 300.0,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
    timeout: float = 15.0,
) -> Dict[str, Any]:
    """Run an audited Slate/Automation/PIE/GameFeature interaction as a durable polling Job."""
    if not allow_runtime_side_effects:
        return {
            "success": False,
            "phase": "policy",
            "retryable": False,
            "error": "allow_runtime_side_effects=true is required",
        }
    stable_payload = json.dumps(
        {"toolset": toolset, "tool": tool, "arguments": arguments or {}},
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    )
    call_seed = idempotency_key or str(uuid.uuid4())
    call_id = "official-runtime-" + hashlib.sha256(
        f"{call_seed}\n{stable_payload}".encode("utf-8")
    ).hexdigest()[:32]
    start, poll = _official_toolset_scripts(
        call_id=call_id,
        toolset=toolset,
        tool=tool,
        arguments=arguments or {},
        access="RuntimeInteraction",
    )
    result = bridge_submit_job(
        start,
        idempotency_key=idempotency_key,
        queue_timeout=queue_timeout,
        poll_code=poll,
        poll_interval=poll_interval,
        run_timeout=run_timeout,
        endpoint=endpoint,
        project=project,
        token=token,
        timeout=timeout,
        no_preflight=True,
    )
    if result.get("success"):
        result = dict(result)
        result.update(
            provider="EpicToolsetRegistry",
            toolset=toolset,
            tool=tool,
            risk="RuntimeInteraction",
            execution="PollingJob",
            save_behavior="Never",
        )
    return result


@mcp.tool()
@_with_catalog()
def bridge_call_official_transactional(
    toolset: str,
    tool: str,
    arguments: Optional[Dict[str, Any]] = None,
    target_packages: Optional[List[str]] = None,
    apply: bool = False,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
    timeout: float = 60.0,
) -> Dict[str, Any]:
    """Preview or apply one audited immediate official mutation; never saves packages."""
    targets = [str(item) for item in (target_packages or []) if str(item).strip()]
    if not targets:
        return {
            "success": False,
            "phase": "policy",
            "retryable": False,
            "error": "target_packages requires at least one explicit package",
        }
    payload = {
        "toolset": toolset,
        "tool": tool,
        "arguments": arguments or {},
        "target_packages": targets,
        "apply": bool(apply),
    }
    encoded = base64.b64encode(
        json.dumps(payload, ensure_ascii=False, sort_keys=True).encode("utf-8")
    ).decode("ascii")
    code = textwrap.dedent(
        f"""
        import base64
        import json
        import unreal
        _p = json.loads(base64.b64decode({encoded!r}).decode("utf-8"))
        _result = json.loads(
            unreal.UnrealBridgeUE58Library.execute_official_transactional_toolset_call(
                _p["toolset"], _p["tool"],
                json.dumps(_p["arguments"], ensure_ascii=False),
                _p["target_packages"], _p["apply"]))
        print(json.dumps(_result, ensure_ascii=False))
        """
    ).strip()
    response = bridge_exec(
        code,
        endpoint=endpoint,
        project=project,
        token=token,
        timeout=timeout,
        no_preflight=True,
    )
    payload_result = _last_json_output(response)
    if payload_result is None:
        return response
    combined = dict(response)
    combined["official_result"] = payload_result
    combined["success"] = bool(response.get("success")) and bool(
        payload_result.get("success")
    )
    if payload_result.get("error"):
        combined["error"] = payload_result["error"]
    change_set = payload_result.get("change_set")
    if isinstance(change_set, dict) and change_set.get("change_set_id"):
        combined["change_set_id"] = change_set["change_set_id"]
        combined["change_set_status"] = change_set.get("status")
    combined.update(
        provider="EpicToolsetRegistry",
        toolset=toolset,
        tool=tool,
        risk="TransactionalSync",
        save_behavior="Never",
        apply=bool(apply),
        rollback_required=bool(apply),
    )
    return combined


@mcp.tool()
@_with_catalog()
def bridge_call_official_transactional_batch(
    calls: List[Dict[str, Any]],
    target_packages: Optional[List[str]] = None,
    apply: bool = False,
    allow_readbacks: bool = False,
    compile_niagara_system_path: Optional[str] = None,
    niagara_user_parameter_renames: Optional[List[Dict[str, str]]] = None,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
    timeout: float = 120.0,
) -> Dict[str, Any]:
    """Execute 1-64 exact official calls atomically in one unsaved ChangeSet.

    By default every call must be TransactionalSync.  High-level graph
    workflows may set ``allow_readbacks`` to interleave audited ReadOnly calls
    before/after mutations, so preview evidence is captured before rollback.
    RuntimeInteraction and Rejected calls are never accepted here.
    """
    targets = [str(item) for item in (target_packages or []) if str(item).strip()]
    if not targets:
        return {
            "success": False,
            "phase": "policy",
            "retryable": False,
            "error": "target_packages requires at least one explicit package",
        }
    renames = list(niagara_user_parameter_renames or [])
    if not isinstance(calls, list) or len(calls) > 64:
        return {
            "success": False,
            "phase": "schema",
            "retryable": False,
            "error": "calls may contain at most 64 objects",
        }
    if not calls and not renames:
        return {
            "success": False,
            "phase": "schema",
            "retryable": False,
            "error": "the batch requires at least one call or Niagara rename",
        }
    if len(renames) > 16:
        return {
            "success": False,
            "phase": "schema",
            "retryable": False,
            "error": "at most 16 Niagara user-parameter renames are allowed",
        }
    normalized_renames: List[Dict[str, str]] = []
    for index, rename in enumerate(renames):
        if not isinstance(rename, dict):
            return {
                "success": False,
                "phase": "schema",
                "retryable": False,
                "error": f"niagara_user_parameter_renames[{index}] must be an object",
            }
        old_name = str(rename.get("old_name") or "").strip()
        new_name = str(rename.get("new_name") or "").strip()
        if (
            not old_name.startswith("User.")
            or not new_name.startswith("User.")
            or old_name == new_name
        ):
            return {
                "success": False,
                "phase": "schema",
                "retryable": False,
                "error": (
                    f"niagara_user_parameter_renames[{index}] requires "
                    "distinct User.* old_name/new_name values"
                ),
            }
        normalized_renames.append(
            {"old_name": old_name, "new_name": new_name}
        )
    normalized_calls: List[Dict[str, Any]] = []
    operation_ids: List[str] = []
    mutation_count = 0
    readback_count = 0
    for index, call in enumerate(calls):
        if not isinstance(call, dict):
            return {
                "success": False,
                "phase": "schema",
                "retryable": False,
                "error": f"calls[{index}] must be an object",
            }
        try:
            if call.get("operation"):
                resolved = _current_catalog().domain_registry.resolve(
                    str(call.get("domain", "")), str(call["operation"])
                )
            else:
                resolved = _current_catalog().domain_registry.resolve(
                    str(call.get("domain") or "editor"),
                    f"{call.get('toolset', '')}|{call.get('tool', '')}",
                )
        except KeyError as exc:
            # Exact toolset/tool pairs can belong to a domain other than the
            # caller's default. Resolve from the complete exact-id index.
            compound = (
                f"{call.get('toolset', '')}|{call.get('tool', '')}".lower()
            )
            resolved = _current_catalog().domain_registry.by_compound.get(compound)
            if resolved is None:
                return {
                    "success": False,
                    "phase": "domain",
                    "retryable": False,
                    "error": f"calls[{index}]: {exc}",
                }
        arguments = dict(call.get("arguments") or {})
        errors = _current_catalog().domain_registry.validate(resolved, arguments)
        if errors:
            return {
                "success": False,
                "phase": "schema",
                "retryable": False,
                "error": f"calls[{index}]: {errors[0]}",
                "errors": errors,
                "operation_id": resolved.id,
            }
        allowed_access = {"TransactionalSync"}
        if allow_readbacks:
            allowed_access.add("ReadOnly")
        if resolved.access not in allowed_access:
            return {
                "success": False,
                "phase": "policy",
                "retryable": False,
                "error": (
                    f"calls[{index}] {resolved.id} is {resolved.access}; "
                    f"allowed access is {sorted(allowed_access)!r}"
                ),
            }
        if resolved.access == "TransactionalSync":
            mutation_count += 1
        else:
            readback_count += 1
        normalized_calls.append(
            {
                "toolset": resolved.toolset,
                "tool": resolved.tool,
                "arguments": arguments,
            }
        )
        operation_ids.append(resolved.id)

    if mutation_count == 0 and not normalized_renames:
        return {
            "success": False,
            "phase": "policy",
            "retryable": False,
            "error": "transactional batch requires at least one TransactionalSync call",
        }

    payload = {
        "calls": normalized_calls,
        "target_packages": targets,
        "apply": bool(apply),
        "compile_niagara_system_path": str(
            compile_niagara_system_path or ""
        ).strip(),
        "niagara_user_parameter_renames": normalized_renames,
    }
    encoded = base64.b64encode(
        json.dumps(payload, ensure_ascii=False, sort_keys=True).encode("utf-8")
    ).decode("ascii")
    code = textwrap.dedent(
        f"""
        import base64
        import json
        import unreal
        _p = json.loads(base64.b64decode({encoded!r}).decode("utf-8"))
        _result = json.loads(
            unreal.UnrealBridgeUE58Library.execute_official_transactional_toolset_batch(
                json.dumps(_p["calls"], ensure_ascii=False),
                _p["target_packages"], _p["apply"],
                _p["compile_niagara_system_path"],
                json.dumps(_p["niagara_user_parameter_renames"],
                           ensure_ascii=False)))
        print(json.dumps(_result, ensure_ascii=False))
        """
    ).strip()
    response = bridge_exec(
        code,
        endpoint=endpoint,
        project=project,
        token=token,
        timeout=timeout,
        no_preflight=True,
    )
    payload_result = _last_json_output(response)
    if payload_result is None:
        return response
    combined = dict(response)
    combined["official_result"] = payload_result
    combined["success"] = bool(response.get("success")) and bool(
        payload_result.get("success")
    )
    if payload_result.get("error"):
        combined["error"] = payload_result["error"]
    change_set = payload_result.get("change_set")
    if isinstance(change_set, dict) and change_set.get("change_set_id"):
        combined["change_set_id"] = change_set["change_set_id"]
        combined["change_set_status"] = change_set.get("status")
    combined.update(
        provider="EpicToolsetRegistry",
        operation_ids=operation_ids,
        mutation_count=mutation_count,
        native_niagara_rename_count=len(normalized_renames),
        readback_count=readback_count,
        risk="TransactionalSync",
        save_behavior="Never",
        apply=bool(apply),
        rollback_required=bool(apply),
    )
    return combined


_TYPED_GRAPH_WORKFLOW_DOMAINS = {
    "blueprint",
    "control_rig",
    "dataflow",
    "material",
    "mesh",
    "pcg",
    "physics",
    "sequencer",
    "ui",
}


@_with_catalog()
def _submit_typed_domain_workflow(
    domain: str,
    calls: List[Dict[str, Any]],
    target_packages: Optional[List[str]],
    apply: bool,
    endpoint: Optional[str],
    project: Optional[str],
    token: Optional[str],
    timeout: float,
) -> Dict[str, Any]:
    canonical = _current_catalog().domain_registry.canonical_domain(domain)
    if canonical not in _TYPED_GRAPH_WORKFLOW_DOMAINS:
        return {
            "success": False,
            "phase": "domain",
            "retryable": False,
            "error": (
                f"{domain!r} is not a transactional graph workflow domain; "
                f"choose one of {sorted(_TYPED_GRAPH_WORKFLOW_DOMAINS)!r}"
            ),
        }
    if not isinstance(calls, list) or not calls:
        return {
            "success": False,
            "phase": "schema",
            "retryable": False,
            "error": "calls must contain ordered before/mutate/readback operations",
        }
    normalized: List[Dict[str, Any]] = []
    access_sequence: List[str] = []
    for index, call in enumerate(calls):
        if not isinstance(call, dict) or not str(call.get("operation") or "").strip():
            return {
                "success": False,
                "phase": "schema",
                "retryable": False,
                "error": f"calls[{index}] requires operation and arguments",
            }
        operation_name = str(call["operation"])
        try:
            resolved = _current_catalog().domain_registry.resolve(canonical, operation_name)
        except KeyError as exc:
            return {
                "success": False,
                "phase": "domain",
                "retryable": False,
                "error": f"calls[{index}]: {exc}",
            }
        arguments = dict(call.get("arguments") or {})
        errors = _current_catalog().domain_registry.validate(resolved, arguments)
        if errors:
            return {
                "success": False,
                "phase": "schema",
                "retryable": False,
                "operation_id": resolved.id,
                "errors": errors,
                "error": f"calls[{index}]: {errors[0]}",
            }
        if resolved.access not in {"ReadOnly", "TransactionalSync"}:
            return {
                "success": False,
                "phase": "policy",
                "retryable": False,
                "operation_id": resolved.id,
                "error": (
                    f"calls[{index}] is {resolved.access}; graph workflows only "
                    "accept immediate ReadOnly/TransactionalSync operations"
                ),
            }
        normalized.append(
            {
                "domain": canonical,
                "operation": operation_name,
                "arguments": arguments,
            }
        )
        access_sequence.append(resolved.access)
    result = bridge_call_official_transactional_batch(
        normalized,
        target_packages=target_packages,
        apply=apply,
        allow_readbacks=True,
        endpoint=endpoint,
        project=project,
        token=token,
        timeout=timeout,
    )
    result = dict(result)
    result.update(
        workflow="typed-domain-transaction",
        workflow_domain=canonical,
        access_sequence=access_sequence,
        apply=bool(apply),
        save_behavior="Never",
    )
    if result.get("success"):
        record = _artifact_store_for(
            endpoint=endpoint, project=project, token=token
        ).put(result, kind=f"{canonical}-transaction-workflow")
        result["workflow_artifact"] = record.as_dict()
    return result


@mcp.tool()
def bridge_submit_typed_domain_workflow(
    domain: str,
    calls: List[Dict[str, Any]],
    target_packages: Optional[List[str]] = None,
    apply: bool = False,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
    timeout: float = 180.0,
) -> Dict[str, Any]:
    """Run before/mutate/readback graph calls in one preview-by-default ChangeSet."""
    return _submit_typed_domain_workflow(
        domain, calls, target_packages, apply, endpoint, project, token, timeout
    )


def _named_graph_workflow(
    domain: str,
    calls: List[Dict[str, Any]],
    target_packages: Optional[List[str]],
    apply: bool,
    endpoint: Optional[str],
    project: Optional[str],
    token: Optional[str],
    timeout: float,
) -> Dict[str, Any]:
    return _submit_typed_domain_workflow(
        domain, calls, target_packages, apply, endpoint, project, token, timeout
    )


@mcp.tool()
def bridge_submit_control_rig_workflow(
    calls: List[Dict[str, Any]], target_packages: List[str], apply: bool = False,
    endpoint: Optional[str] = None, project: Optional[str] = None,
    token: Optional[str] = None, timeout: float = 180.0,
) -> Dict[str, Any]:
    """Execute typed Control Rig/RigVM graph edits plus before/after readback."""
    return _named_graph_workflow("control_rig", calls, target_packages, apply, endpoint, project, token, timeout)


@mcp.tool()
def bridge_submit_sequencer_workflow(
    calls: List[Dict[str, Any]], target_packages: List[str], apply: bool = False,
    endpoint: Optional[str] = None, project: Optional[str] = None,
    token: Optional[str] = None, timeout: float = 180.0,
) -> Dict[str, Any]:
    """Execute typed Sequencer/ControlRig track edits plus before/after readback."""
    return _named_graph_workflow("sequencer", calls, target_packages, apply, endpoint, project, token, timeout)


@mcp.tool()
def bridge_submit_pcg_workflow(
    calls: List[Dict[str, Any]], target_packages: List[str], apply: bool = False,
    endpoint: Optional[str] = None, project: Optional[str] = None,
    token: Optional[str] = None, timeout: float = 180.0,
) -> Dict[str, Any]:
    """Execute typed PCG graph CRUD plus schema/node-data readback."""
    return _named_graph_workflow("pcg", calls, target_packages, apply, endpoint, project, token, timeout)


@mcp.tool()
def bridge_submit_physics_workflow(
    calls: List[Dict[str, Any]], target_packages: List[str], apply: bool = False,
    endpoint: Optional[str] = None, project: Optional[str] = None,
    token: Optional[str] = None, timeout: float = 180.0,
) -> Dict[str, Any]:
    """Execute typed PhysicsAsset body/shape/constraint edits plus readback."""
    return _named_graph_workflow("physics", calls, target_packages, apply, endpoint, project, token, timeout)


@mcp.tool()
def bridge_submit_dataflow_workflow(
    calls: List[Dict[str, Any]], target_packages: List[str], apply: bool = False,
    endpoint: Optional[str] = None, project: Optional[str] = None,
    token: Optional[str] = None, timeout: float = 180.0,
) -> Dict[str, Any]:
    """Execute typed Dataflow graph/variable edits plus schema readback."""
    return _named_graph_workflow("dataflow", calls, target_packages, apply, endpoint, project, token, timeout)


@mcp.tool()
def bridge_submit_mesh_workflow(
    calls: List[Dict[str, Any]], target_packages: List[str], apply: bool = False,
    endpoint: Optional[str] = None, project: Optional[str] = None,
    token: Optional[str] = None, timeout: float = 180.0,
) -> Dict[str, Any]:
    """Execute typed Skeletal/StaticMesh edits plus collision/LOD/material readback."""
    return _named_graph_workflow("mesh", calls, target_packages, apply, endpoint, project, token, timeout)


@mcp.tool()
@_with_catalog()
def bridge_call_domain_operation(
    domain: str,
    operation: str,
    arguments: Optional[Dict[str, Any]] = None,
    target_packages: Optional[List[str]] = None,
    apply: bool = False,
    allow_runtime_side_effects: bool = False,
    idempotency_key: Optional[str] = None,
    queue_timeout: float = 300.0,
    poll_interval: float = 0.25,
    run_timeout: float = 300.0,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
    timeout: float = 15.0,
) -> Dict[str, Any]:
    """Validate and dispatch one official operation through its audited execution plane.

    ReadOnly and RuntimeInteraction operations return a durable ``job_id``.
    TransactionalSync operations execute immediately inside an explicit
    ChangeSet, default to preview, and never save a package.  Rejected
    operations cannot be dispatched through this generic adapter.
    """
    try:
        resolved = _current_catalog().domain_registry.resolve(domain, operation)
    except KeyError as exc:
        return {
            "success": False,
            "phase": "domain",
            "retryable": False,
            "error": str(exc),
        }
    call_arguments = dict(arguments or {})
    validation_errors = _current_catalog().domain_registry.validate(
        resolved, call_arguments
    )
    if validation_errors:
        return {
            "success": False,
            "phase": "schema",
            "retryable": False,
            "operation_id": resolved.id,
            "schema_sha256": resolved.schema_sha256,
            "errors": validation_errors,
            "error": validation_errors[0],
        }
    common = {
        "operation_id": resolved.id,
        "domain": _current_catalog().domain_registry.canonical_domain(domain),
        "access": resolved.access,
        "schema_sha256": resolved.schema_sha256,
    }
    if resolved.access == "Rejected":
        return {
            "success": False,
            "phase": "policy",
            "retryable": False,
            "error": (
                f"{resolved.id} is rejected by the exact UE 5.8 policy; "
                "use a dedicated reviewed workflow"
            ),
            **common,
        }
    if resolved.access == "ReadOnly":
        result = bridge_submit_official_toolset_job(
            resolved.toolset,
            resolved.tool,
            call_arguments,
            idempotency_key=idempotency_key,
            queue_timeout=queue_timeout,
            poll_interval=poll_interval,
            run_timeout=run_timeout,
            endpoint=endpoint,
            project=project,
            token=token,
            timeout=timeout,
        )
    elif resolved.access == "RuntimeInteraction":
        result = bridge_submit_official_runtime_job(
            resolved.toolset,
            resolved.tool,
            call_arguments,
            allow_runtime_side_effects=allow_runtime_side_effects,
            idempotency_key=idempotency_key,
            queue_timeout=queue_timeout,
            poll_interval=poll_interval,
            run_timeout=run_timeout,
            endpoint=endpoint,
            project=project,
            token=token,
            timeout=timeout,
        )
    elif resolved.access == "TransactionalSync":
        result = bridge_call_official_transactional(
            resolved.toolset,
            resolved.tool,
            call_arguments,
            target_packages=target_packages,
            apply=apply,
            endpoint=endpoint,
            project=project,
            token=token,
            timeout=max(timeout, 60.0),
        )
    else:
        return {
            "success": False,
            "phase": "policy",
            "retryable": False,
            "error": f"unsupported generated access plane {resolved.access!r}",
            **common,
        }
    response = dict(result)
    response.update(common)
    return response


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


def _wait_scenario_job(
    submitted: Dict[str, Any],
    *,
    deadline: float,
    endpoint: Optional[str],
    project: Optional[str],
    token: Optional[str],
) -> Dict[str, Any]:
    if not submitted.get("success") or not submitted.get("job_id"):
        raise RuntimeError(submitted.get("error", "job submission failed"))
    job_id = str(submitted["job_id"])
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError(f"job {job_id} did not finish before the scenario step deadline")
        snapshot = bridge_wait_job(
            job_id,
            wait_timeout=min(30.0, remaining),
            endpoint=endpoint,
            project=project,
            token=token,
        )
        if snapshot.get("terminal"):
            if snapshot.get("job_state") != "succeeded":
                raise RuntimeError(snapshot.get("error") or f"job {job_id} ended as {snapshot.get('job_state')}")
            result = dict(snapshot)
            official_payload = _last_json_output(snapshot)
            if official_payload is not None:
                result["official_result"] = official_payload
                # Native high-level Jobs (Material, MetaSound, and similar
                # workflows) use the same JSON result convention as official
                # Toolset Jobs.  Promote the transaction metadata so the
                # Scenario rollback hook can see it without understanding the
                # payload's implementation-specific shape.
                for key in (
                    "change_set_id",
                    "changeset_id",
                    "rollback_required",
                    "save_behavior",
                    "apply",
                ):
                    if key in official_payload:
                        result[key] = official_payload[key]
                if official_payload.get("success") is False:
                    raise RuntimeError(
                        official_payload.get("error")
                        or f"official tool in job {job_id} failed"
                    )
            return result


def _scenario_manager(
    *,
    endpoint: Optional[str],
    project: Optional[str],
    token: Optional[str],
) -> ScenarioManager:
    artifact_store = _artifact_store_for(endpoint=endpoint, project=project, token=token)
    scenario_root = artifact_store.root / "scenarios"

    def execute_step(step: Dict[str, Any], timeout: float) -> Dict[str, Any]:
        step_type = str(step.get("type", "call"))
        deadline = time.monotonic() + timeout
        if step_type == "call":
            result = bridge_call(
                str(step.get("library", "")),
                str(step.get("function", "")),
                dict(step.get("kwargs") or {}),
                endpoint=endpoint,
                project=project,
                token=token,
                timeout=timeout,
            )
        elif step_type == "exec":
            result = bridge_exec(
                str(step.get("code", "")),
                endpoint=endpoint,
                project=project,
                token=token,
                timeout=timeout,
                no_preflight=bool(step.get("no_preflight", False)),
                idempotency_key=step.get("idempotency_key"),
            )
        elif step_type == "job":
            submitted = bridge_submit_job(
                str(step.get("code", "")),
                idempotency_key=step.get("idempotency_key"),
                queue_timeout=float(step.get("queue_timeout", 300.0)),
                poll_code=step.get("poll_code"),
                poll_interval=float(step.get("poll_interval", 0.25)),
                run_timeout=float(step.get("run_timeout", timeout)),
                endpoint=endpoint,
                project=project,
                token=token,
                timeout=min(15.0, timeout),
                no_preflight=bool(step.get("no_preflight", False)),
            )
            result = _wait_scenario_job(
                submitted,
                deadline=deadline,
                endpoint=endpoint,
                project=project,
                token=token,
            )
        elif step_type == "official":
            submitted = bridge_submit_official_toolset_job(
                str(step.get("toolset", "")),
                str(step.get("tool", "")),
                dict(step.get("arguments") or {}),
                idempotency_key=step.get("idempotency_key"),
                queue_timeout=float(step.get("queue_timeout", 300.0)),
                poll_interval=float(step.get("poll_interval", 0.25)),
                run_timeout=float(step.get("run_timeout", timeout)),
                endpoint=endpoint,
                project=project,
                token=token,
                timeout=min(15.0, timeout),
            )
            result = _wait_scenario_job(
                submitted,
                deadline=deadline,
                endpoint=endpoint,
                project=project,
                token=token,
            )
        elif step_type == "official_runtime":
            submitted = bridge_submit_official_runtime_job(
                str(step.get("toolset", "")),
                str(step.get("tool", "")),
                dict(step.get("arguments") or {}),
                allow_runtime_side_effects=bool(
                    step.get("allow_runtime_side_effects", False)
                ),
                idempotency_key=step.get("idempotency_key"),
                queue_timeout=float(step.get("queue_timeout", 300.0)),
                poll_interval=float(step.get("poll_interval", 0.25)),
                run_timeout=float(step.get("run_timeout", timeout)),
                endpoint=endpoint,
                project=project,
                token=token,
                timeout=min(15.0, timeout),
            )
            result = _wait_scenario_job(
                submitted,
                deadline=deadline,
                endpoint=endpoint,
                project=project,
                token=token,
            )
            result = dict(result)
            result["rollback_required"] = False
        elif step_type == "official_transactional":
            result = bridge_call_official_transactional(
                str(step.get("toolset", "")),
                str(step.get("tool", "")),
                dict(step.get("arguments") or {}),
                target_packages=list(step.get("target_packages") or []),
                apply=bool(step.get("apply", False)),
                endpoint=endpoint,
                project=project,
                token=token,
                timeout=timeout,
            )
        elif step_type == "official_transactional_batch":
            result = bridge_call_official_transactional_batch(
                list(step.get("calls") or []),
                target_packages=list(step.get("target_packages") or []),
                apply=bool(step.get("apply", False)),
                allow_readbacks=bool(step.get("allow_readbacks", False)),
                compile_niagara_system_path=step.get(
                    "compile_niagara_system_path"
                ),
                niagara_user_parameter_renames=list(
                    step.get("niagara_user_parameter_renames") or []
                ),
                endpoint=endpoint,
                project=project,
                token=token,
                timeout=timeout,
            )
        elif step_type == "niagara_input_guard":
            submitted = bridge_call_domain_operation(
                "niagara",
                "GetStackInputData",
                {"stackInputRef": dict(step.get("stack_input_ref") or {})},
                idempotency_key=step.get("idempotency_key"),
                run_timeout=timeout,
                endpoint=endpoint,
                project=project,
                token=token,
                timeout=min(15.0, timeout),
            )
            result = _wait_scenario_job(
                submitted,
                deadline=deadline,
                endpoint=endpoint,
                project=project,
                token=token,
            )
            payload = result.get("official_result") or {}
            binding_kind = existing_binding_kind(payload.get("result"))
            replace_existing = bool(step.get("replace_existing", False))
            if not replace_existing and binding_kind != "Literal":
                raise RuntimeError(
                    "Niagara input already has a non-literal or unknown binding "
                    f"({binding_kind}); set replace_existing=true explicitly"
                )
            result = dict(result)
            result.update(
                binding_kind=binding_kind,
                replace_existing=replace_existing,
                rollback_required=False,
            )
        elif step_type == "niagara_binding_tree":
            stack_ref = dict(step.get("stack_input_ref") or {})

            def niagara_read(tool_name: str) -> Dict[str, Any]:
                submitted = bridge_call_domain_operation(
                    "niagara",
                    tool_name,
                    {"stackInputRef": stack_ref},
                    run_timeout=timeout,
                    endpoint=endpoint,
                    project=project,
                    token=token,
                    timeout=min(15.0, timeout),
                )
                return _wait_scenario_job(
                    submitted,
                    deadline=deadline,
                    endpoint=endpoint,
                    project=project,
                    token=token,
                )

            topology = niagara_read("GetStackInputTopology")
            value = niagara_read("GetStackInputData")
            topology_payload = topology.get("official_result") or {}
            topology_result = topology_payload.get("result") or {}
            topology_value = (
                topology_result.get("returnValue")
                if isinstance(topology_result, dict)
                else None
            )
            dynamic = None
            if isinstance(topology_value, dict) and topology_value.get("bIsDynamic"):
                dynamic = niagara_read("GetDynamicInputChain")
            result = {
                "success": True,
                "stack_input_ref": stack_ref,
                "topology": topology,
                "value": value,
                "dynamic_chain": dynamic,
                "is_dynamic": bool(
                    isinstance(topology_value, dict)
                    and topology_value.get("bIsDynamic")
                ),
                "rollback_required": False,
            }
        elif step_type == "niagara_wait_compile":
            system_path = str(step.get("system_path", ""))
            last_result: Dict[str, Any] = {}
            poll_count = 0
            while True:
                if time.monotonic() >= deadline:
                    raise TimeoutError(
                        f"Niagara compile did not settle for {system_path!r}"
                    )
                submitted = bridge_call_domain_operation(
                    "niagara",
                    "GetSystemCompileState",
                    {"system": object_ref(system_path)},
                    idempotency_key=f"{step.get('idempotency_key') or step.get('id')}-{poll_count}",
                    run_timeout=min(60.0, timeout),
                    endpoint=endpoint,
                    project=project,
                    token=token,
                    timeout=min(15.0, timeout),
                )
                last_result = _wait_scenario_job(
                    submitted,
                    deadline=deadline,
                    endpoint=endpoint,
                    project=project,
                    token=token,
                )
                poll_count += 1
                payload = last_result.get("official_result") or {}
                tool_result = payload.get("result") or {}
                compile_state = (
                    tool_result.get("returnValue")
                    if isinstance(tool_result, dict)
                    else None
                )
                if isinstance(compile_state, dict) and not compile_state.get(
                    "bIsCompiling", False
                ):
                    result = dict(last_result)
                    result.update(
                        compile_state=compile_state,
                        poll_count=poll_count,
                        rollback_required=False,
                    )
                    break
                time.sleep(min(0.5, max(0.05, deadline - time.monotonic())))
        elif step_type == "slate_capture":
            submitted = bridge_call_domain_operation(
                "ui",
                "Screenshot",
                {"ref": str(step.get("ref", ""))},
                allow_runtime_side_effects=True,
                run_timeout=timeout,
                endpoint=endpoint,
                project=project,
                token=token,
                timeout=min(15.0, timeout),
            )
            captured = _wait_scenario_job(
                submitted,
                deadline=deadline,
                endpoint=endpoint,
                project=project,
                token=token,
            )
            payload = captured.get("official_result") or {}
            tool_result = payload.get("result") or {}
            image_value = (
                tool_result.get("returnValue")
                if isinstance(tool_result, dict)
                else None
            )
            if not isinstance(image_value, dict):
                raise RuntimeError("Slate Screenshot returned no ToolsetImage")
            encoded_image = str(image_value.get("data") or "")
            mime_type = str(image_value.get("mimeType") or "image/png")
            if len(encoded_image) > 128 * 1024 * 1024:
                raise RuntimeError("Slate Screenshot exceeds the 96 MiB decoded limit")
            try:
                image_bytes = base64.b64decode(encoded_image, validate=True)
            except (ValueError, TypeError) as exc:
                raise RuntimeError("Slate Screenshot returned invalid base64") from exc
            if not image_bytes or len(image_bytes) > 96 * 1024 * 1024:
                raise RuntimeError("Slate Screenshot is empty or exceeds 96 MiB")
            image_record = artifact_store.put(
                image_bytes, kind="slate-screenshot", media_type=mime_type
            )
            result = {
                "success": True,
                "ref": str(step.get("ref", "")),
                "image_artifact": image_record.as_dict(),
                "rollback_required": False,
            }
            expected_path = str(step.get("expected_path") or "").strip()
            if expected_path:
                if compare_golden_images is None:
                    raise RuntimeError(
                        "golden-image dependencies are unavailable: "
                        + GOLDEN_IMAGE_IMPORT_ERROR
                    )
                comparison, diff_png = compare_golden_images(
                    Path(expected_path),
                    Path(image_record.path),
                    pixel_delta=int(step.get("pixel_delta", 8)),
                    max_mae=float(step.get("max_mae", 0.01)),
                    max_changed_ratio=float(step.get("max_changed_ratio", 0.01)),
                    make_diff=True,
                )
                result["golden"] = comparison
                result["golden_report_artifact"] = artifact_store.put(
                    comparison, kind="golden-image-report"
                ).as_dict()
                if diff_png is not None:
                    result["golden_diff_artifact"] = artifact_store.put(
                        diff_png,
                        kind="golden-image-diff",
                        media_type="image/png",
                    ).as_dict()
                if bool(step.get("fail_on_diff", True)) and not comparison.get(
                    "passed", False
                ):
                    raise AssertionError("Slate screenshot differs from golden thresholds")
        elif step_type == "game_feature_wait":
            plugin_name = str(step.get("plugin_name") or "").strip()
            expected_active = bool(step.get("expected_active", True))
            if not plugin_name:
                raise ValueError("game_feature_wait requires plugin_name")
            poll_count = 0
            last_snapshot: Dict[str, Any] = {}
            while True:
                if time.monotonic() >= deadline:
                    state = last_snapshot.get("state", "Unknown")
                    raise TimeoutError(
                        f"GameFeature {plugin_name!r} did not reach "
                        f"active={expected_active}; last state={state!r}"
                    )
                response = bridge_call(
                    "UnrealBridgeGameFeatureLibrary",
                    "get_game_feature_info",
                    {
                        "plugin_name_or_url": plugin_name,
                        "run_data_validation": bool(
                            step.get("run_data_validation", True)
                        ),
                    },
                    endpoint=endpoint,
                    project=project,
                    token=token,
                    timeout=min(30.0, max(1.0, deadline - time.monotonic())),
                )
                payload = _last_json_output(response) or {}
                snapshot = payload.get("result")
                if not isinstance(snapshot, dict):
                    raise RuntimeError(
                        "GameFeature native diagnostic returned no structured result"
                    )
                last_snapshot = snapshot
                poll_count += 1
                active = bool(snapshot.get("active", snapshot.get("b_active", False)))
                error_state = bool(
                    snapshot.get(
                        "error_state", snapshot.get("b_in_error_state", False)
                    )
                )
                if active == expected_active:
                    if error_state:
                        raise RuntimeError(
                            f"GameFeature {plugin_name!r} entered an error state"
                        )
                    result = {
                        "success": True,
                        "plugin_name": plugin_name,
                        "expected_active": expected_active,
                        "poll_count": poll_count,
                        "state": snapshot.get("state", "Unknown"),
                        "diagnostic": snapshot,
                        "transport": response,
                        "rollback_required": False,
                    }
                    break
                if error_state:
                    raise RuntimeError(
                        f"GameFeature {plugin_name!r} entered state "
                        f"{snapshot.get('state', 'Unknown')!r} with error_state=true"
                    )
                time.sleep(min(0.25, max(0.05, deadline - time.monotonic())))
        elif step_type == "domain":
            result = bridge_call_domain_operation(
                str(step.get("domain", "")),
                str(step.get("operation", "")),
                dict(step.get("arguments") or {}),
                target_packages=list(step.get("target_packages") or []),
                apply=bool(step.get("apply", False)),
                allow_runtime_side_effects=bool(
                    step.get("allow_runtime_side_effects", False)
                ),
                idempotency_key=step.get("idempotency_key"),
                queue_timeout=float(step.get("queue_timeout", 300.0)),
                poll_interval=float(step.get("poll_interval", 0.25)),
                run_timeout=float(step.get("run_timeout", timeout)),
                endpoint=endpoint,
                project=project,
                token=token,
                timeout=min(15.0, timeout),
            )
            if result.get("success") and result.get("job_id"):
                result = _wait_scenario_job(
                    result,
                    deadline=deadline,
                    endpoint=endpoint,
                    project=project,
                    token=token,
                )
                result = dict(result)
                result["rollback_required"] = False
        elif step_type == "golden_image":
            result = bridge_compare_golden_image(
                str(step.get("expected_path", "")),
                str(step.get("actual_path", "")),
                pixel_delta=int(step.get("pixel_delta", 8)),
                max_mae=float(step.get("max_mae", 0.01)),
                max_changed_ratio=float(step.get("max_changed_ratio", 0.01)),
                endpoint=endpoint,
                project=project,
                token=token,
            )
        else:
            raise ValueError(f"unsupported scenario step type {step_type!r}")
        if not isinstance(result, dict) or result.get("success") is False:
            raise RuntimeError((result or {}).get("error", f"scenario step {step.get('id')} failed"))
        if step.get("artifact"):
            artifact = artifact_store.put(result, kind=f"scenario-step-{step.get('id')}")
            result = dict(result)
            result["artifact"] = artifact.as_dict()
        return result

    def rollback_step(step: Dict[str, Any], output: Dict[str, Any]) -> Dict[str, Any]:
        change_set_id = output.get("change_set_id") or output.get("changeset_id")
        if not change_set_id:
            return {
                "success": bool(step.get("preview", False)),
                "skipped": True,
                "reason": "step returned no committed change_set_id",
            }
        code = (
            "import json, unreal\n"
            f"_r=unreal.UnrealBridgeChangeSetLibrary.rollback_committed_change_set({change_set_id!r})\n"
            "print(json.dumps({'success': bool(_r.success), 'status': _r.status, "
            "'error': _r.error, 'change_set_id': _r.change_set_id}, ensure_ascii=False))"
        )
        response = bridge_exec(
            code,
            endpoint=endpoint,
            project=project,
            token=token,
            timeout=30.0,
            no_preflight=True,
        )
        payload = _last_json_output(response)
        if payload is None:
            return response
        combined = dict(response)
        combined.update(payload)
        combined["success"] = bool(response.get("success")) and bool(
            payload.get("success")
        )
        return combined

    return ScenarioManager(scenario_root, execute_step, rollback_step)


@mcp.tool()
@_with_catalog()
def bridge_submit_scenario(
    scenario: Dict[str, Any],
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
) -> Dict[str, Any]:
    """Submit a persisted multi-step scenario with assertions, safe retries, and rollback hooks."""
    try:
        normalized = copy.deepcopy(scenario)
        for step in normalized.get("steps") or []:
            if str(step.get("type", "call")) != "domain":
                continue
            resolved = _current_catalog().domain_registry.resolve(
                str(step.get("domain", "")), str(step.get("operation", ""))
            )
            validation_errors = _current_catalog().domain_registry.validate(
                resolved, dict(step.get("arguments") or {})
            )
            if validation_errors:
                raise ValueError(
                    f"domain step {step.get('id')!r}: {validation_errors[0]}"
                )
            if resolved.access == "Rejected":
                raise ValueError(
                    f"domain step {step.get('id')!r}: {resolved.id} is rejected"
                )
            step["risk"] = (
                "ReadOnly" if resolved.access == "ReadOnly" else resolved.access
            )
            step["resolved_operation_id"] = resolved.id
        manager = _scenario_manager(endpoint=endpoint, project=project, token=token)
        state = manager.submit(normalized)
        with _SCENARIO_MANAGER_LOCK:
            _SCENARIO_MANAGERS[state["scenario_id"]] = manager
        return {
            "success": True,
            "scenario_id": state["scenario_id"],
            "status": state["status"],
            "state_path": str(manager._path(state["scenario_id"])),
        }
    except (KeyError, ValueError, OSError) as exc:
        return {"success": False, "phase": "scenario", "retryable": False, "error": str(exc)}


def _resolve_scenario_manager(
    scenario_id: str,
    *,
    endpoint: Optional[str],
    project: Optional[str],
    token: Optional[str],
) -> ScenarioManager:
    with _SCENARIO_MANAGER_LOCK:
        manager = _SCENARIO_MANAGERS.get(scenario_id)
    if manager:
        return manager
    return _scenario_manager(endpoint=endpoint, project=project, token=token)


@mcp.tool()
def bridge_get_scenario(
    scenario_id: str,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
    fields: Optional[List[str]] = None,
    omit: Optional[List[str]] = None,
    result_options: Optional[Dict[str, Any]] = None,
) -> Dict[str, Any]:
    """Read persisted per-step scenario state after client disconnects."""
    try:
        state = _resolve_scenario_manager(
            scenario_id, endpoint=endpoint, project=project, token=token
        ).get(scenario_id)
        fields, omit = _projection_options(fields, omit, result_options)
        return _shape_response(
            state,
            fields=fields,
            omit=omit,
            max_items=500,
            endpoint=endpoint,
            project=project,
            token=token,
            query_hash=_query_hash({"kind": "scenario", "id": scenario_id}),
        )
    except (ValueError, FileNotFoundError, OSError) as exc:
        return {"success": False, "phase": "scenario", "retryable": False, "error": str(exc)}


@mcp.tool()
def bridge_cancel_scenario(
    scenario_id: str,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
) -> Dict[str, Any]:
    """Request cooperative cancellation at the next scenario step boundary."""
    try:
        state = _resolve_scenario_manager(
            scenario_id, endpoint=endpoint, project=project, token=token
        ).cancel(scenario_id)
        return {"success": True, "scenario_id": scenario_id, "status": state["status"], "cancel_requested": True}
    except (ValueError, FileNotFoundError, OSError) as exc:
        return {"success": False, "phase": "scenario", "retryable": False, "error": str(exc)}


@mcp.tool()
def bridge_submit_automation_run(
    test_names: Optional[List[str]] = None,
    filter_expression: Optional[str] = None,
    force_rediscover: bool = False,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
) -> Dict[str, Any]:
    """Submit durable UE Automation discovery, execution, status, results, and report artifacts."""
    names = [str(item) for item in (test_names or []) if str(item).strip()]
    expression = str(filter_expression or "").strip()
    if bool(names) == bool(expression):
        return {
            "success": False,
            "phase": "automation",
            "retryable": False,
            "error": "provide exactly one of test_names or filter_expression",
        }
    toolset = "AutomationTestToolset.AutomationTestToolset"
    run_tool = "RunTests" if names else "RunTestsByFilter"
    run_arguments: Dict[str, Any] = (
        {"testNames": names} if names else {"filterExpression": expression}
    )
    scenario = {
        "name": f"UE Automation: {run_tool}",
        "metadata": {"kind": "automation", "toolset": toolset},
        "steps": [
            {
                "id": "discover",
                "type": "official_runtime",
                "toolset": toolset,
                "tool": "DiscoverTests",
                "arguments": {"bForceRediscover": bool(force_rediscover)},
                "allow_runtime_side_effects": True,
                "timeout": 300.0,
                "run_timeout": 300.0,
                "artifact": True,
            },
            {
                "id": "run",
                "type": "official_runtime",
                "toolset": toolset,
                "tool": run_tool,
                "arguments": run_arguments,
                "allow_runtime_side_effects": True,
                "timeout": 3600.0,
                "run_timeout": 3600.0,
                "artifact": True,
            },
            {
                "id": "status",
                "type": "official",
                "toolset": toolset,
                "tool": "GetTestStatus",
                "arguments": {},
                "timeout": 120.0,
                "artifact": True,
            },
            {
                "id": "results",
                "type": "official",
                "toolset": toolset,
                "tool": "GetTestResults",
                "arguments": {},
                "timeout": 120.0,
                "artifact": True,
            },
        ],
    }
    submitted = bridge_submit_scenario(
        scenario, endpoint=endpoint, project=project, token=token
    )
    if submitted.get("success"):
        submitted = dict(submitted)
        submitted["automation"] = True
        submitted["run_tool"] = run_tool
    return submitted


@mcp.tool()
def bridge_cancel_automation_run(
    scenario_id: str,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
) -> Dict[str, Any]:
    """Request scenario cancellation and submit Automation StopTests to end an active run."""
    scenario_result = bridge_cancel_scenario(
        scenario_id, endpoint=endpoint, project=project, token=token
    )
    stop_result = bridge_submit_official_runtime_job(
        "AutomationTestToolset.AutomationTestToolset",
        "StopTests",
        {},
        allow_runtime_side_effects=True,
        endpoint=endpoint,
        project=project,
        token=token,
    )
    return {
        "success": bool(scenario_result.get("success"))
        and bool(stop_result.get("success")),
        "scenario": scenario_result,
        "stop_job": stop_result,
    }


@mcp.tool()
@_with_catalog()
def bridge_submit_slate_action(
    action: str,
    arguments: Optional[Dict[str, Any]] = None,
    allow_runtime_side_effects: bool = False,
    idempotency_key: Optional[str] = None,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
    timeout: float = 15.0,
) -> Dict[str, Any]:
    """Validate and submit one semantic Slate action without OS input injection."""
    try:
        resolved = _current_catalog().domain_registry.resolve("ui", action)
        if resolved.toolset != "SlateInspectorToolset.SlateInspectorToolset":
            raise ValueError(f"{resolved.id} is not a SlateInspector operation")
        return bridge_call_domain_operation(
            "ui",
            resolved.id,
            dict(arguments or {}),
            allow_runtime_side_effects=allow_runtime_side_effects,
            idempotency_key=idempotency_key,
            endpoint=endpoint,
            project=project,
            token=token,
            timeout=timeout,
        )
    except (KeyError, ValueError) as exc:
        return {
            "success": False,
            "phase": "slate",
            "retryable": False,
            "error": str(exc),
        }


@mcp.tool()
@_with_catalog()
def bridge_submit_slate_workflow(
    actions: List[Dict[str, Any]],
    root_ref: str = "",
    allow_runtime_side_effects: bool = False,
    capture_before: bool = True,
    capture_after: bool = True,
    expected_path: Optional[str] = None,
    max_depth: int = 30,
    include_source_locations: bool = False,
    pixel_delta: int = 8,
    max_mae: float = 0.01,
    max_changed_ratio: float = 0.01,
    fail_on_diff: bool = True,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
) -> Dict[str, Any]:
    """Run Snapshot -> semantic actions -> Snapshot/Screenshot/Golden as a durable scenario."""
    try:
        if not isinstance(actions, list) or len(actions) > 50:
            raise ValueError("actions must be an array with at most 50 entries")
        normalized: List[Dict[str, Any]] = []
        needs_runtime_opt_in = bool(capture_before or capture_after or expected_path)
        for index, item in enumerate(actions):
            if not isinstance(item, dict):
                raise ValueError(f"actions[{index}] must be an object")
            action = str(item.get("action") or "")
            resolved = _current_catalog().domain_registry.resolve("ui", action)
            if resolved.toolset != "SlateInspectorToolset.SlateInspectorToolset":
                raise ValueError(f"actions[{index}] is not a SlateInspector operation")
            arguments = dict(item.get("arguments") or {})
            errors = _current_catalog().domain_registry.validate(resolved, arguments)
            if errors:
                raise ValueError(f"actions[{index}]: {errors[0]}")
            if resolved.access == "Rejected":
                raise ValueError(f"actions[{index}] {resolved.id} is rejected")
            needs_runtime_opt_in = needs_runtime_opt_in or (
                resolved.access == "RuntimeInteraction"
            )
            normalized.append(
                {
                    "id": f"action_{index}_{resolved.tool}",
                    "type": "domain",
                    "domain": "ui",
                    "operation": resolved.id,
                    "arguments": arguments,
                    "allow_runtime_side_effects": resolved.access
                    == "RuntimeInteraction",
                    "timeout": float(item.get("timeout", 120.0)),
                    "run_timeout": float(item.get("run_timeout", 120.0)),
                    "artifact": bool(item.get("artifact", True)),
                }
            )
        if needs_runtime_opt_in and not allow_runtime_side_effects:
            raise ValueError(
                "allow_runtime_side_effects=true is required for Slate interaction or screenshots"
            )
        snapshot_arguments = {
            "ref": str(root_ref),
            "maxDepth": max(0, min(int(max_depth), 100)),
            "bIncludeSourceLocations": bool(include_source_locations),
        }
        steps: List[Dict[str, Any]] = []
        if capture_before:
            steps.extend(
                [
                    _niagara_read_step(
                        "before_snapshot", "Snapshot", snapshot_arguments
                    )
                    | {"domain": "ui"},
                    {
                        "id": "before_screenshot",
                        "type": "slate_capture",
                        "ref": str(root_ref),
                        "timeout": 120.0,
                    },
                ]
            )
        steps.extend(normalized)
        if capture_after or expected_path:
            steps.append(
                _niagara_read_step(
                    "after_snapshot", "Snapshot", snapshot_arguments
                )
                | {"domain": "ui"}
            )
            steps.append(
                {
                    "id": "after_screenshot",
                    "type": "slate_capture",
                    "ref": str(root_ref),
                    "expected_path": str(expected_path or ""),
                    "pixel_delta": int(pixel_delta),
                    "max_mae": float(max_mae),
                    "max_changed_ratio": float(max_changed_ratio),
                    "fail_on_diff": bool(fail_on_diff),
                    "timeout": 180.0,
                }
            )
        result = bridge_submit_scenario(
            {
                "name": "Slate 3.0 semantic workflow",
                "metadata": {
                    "kind": "slate",
                    "root_ref": root_ref,
                    "input_injection": "Unreal-Slate-only",
                    "golden": bool(expected_path),
                },
                "steps": steps,
            },
            endpoint=endpoint,
            project=project,
            token=token,
        )
        if result.get("success"):
            result = dict(result)
            result.update(
                slate=True,
                semantic_action_count=len(normalized),
                os_input_injection=False,
                golden=bool(expected_path),
            )
        return result
    except (KeyError, TypeError, ValueError) as exc:
        return {
            "success": False,
            "phase": "slate",
            "retryable": False,
            "error": str(exc),
        }


@mcp.tool()
@_with_catalog()
def bridge_submit_game_feature_transition(
    plugin_name: str,
    activate: bool = True,
    allow_runtime_side_effects: bool = False,
    run_data_validation: bool = True,
    timeout: float = 180.0,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
) -> Dict[str, Any]:
    """Validate, request, and verify a GameFeature activation/deactivation as a scenario."""
    try:
        plugin_name = str(plugin_name).strip()
        if not plugin_name:
            raise ValueError("plugin_name is required")
        if not allow_runtime_side_effects:
            raise ValueError(
                "allow_runtime_side_effects=true is required for GameFeature transitions"
            )
        operation = (
            "RequestActivateGameFeature"
            if activate
            else "RequestDeactivateGameFeature"
        )
        resolved = _current_catalog().domain_registry.resolve("game_feature", operation)
        if resolved.toolset != "GameFeaturesToolset.GameFeaturesToolset":
            raise ValueError(f"{resolved.id} is not a GameFeatures Toolset operation")
        arguments = {"pluginName": plugin_name}
        validation_errors = _current_catalog().domain_registry.validate(resolved, arguments)
        if validation_errors:
            raise ValueError(validation_errors[0])
        transition_idempotency_key = (
            f"game-feature:{plugin_name}:"
            f"{'activate' if activate else 'deactivate'}:{uuid.uuid4().hex}"
        )
        result = bridge_submit_scenario(
            {
                "name": (
                    f"GameFeature {'activate' if activate else 'deactivate'}: "
                    f"{plugin_name}"
                ),
                "metadata": {
                    "kind": "game_feature_transition",
                    "plugin_name": plugin_name,
                    "target_active": bool(activate),
                    "save_behavior": "Never",
                },
                "steps": [
                    {
                        "id": "before_diagnostic",
                        "type": "call",
                        "library": "UnrealBridgeGameFeatureLibrary",
                        "function": "get_game_feature_info",
                        "kwargs": {
                            "plugin_name_or_url": plugin_name,
                            "run_data_validation": bool(run_data_validation),
                        },
                        "risk": "ReadOnly",
                        "artifact": True,
                    },
                    {
                        "id": "request_transition",
                        "type": "domain",
                        "domain": "game_feature",
                        "operation": resolved.id,
                        "arguments": arguments,
                        "allow_runtime_side_effects": True,
                        "risk": "RuntimeInteraction",
                        "idempotency_key": transition_idempotency_key,
                        "timeout": min(max(float(timeout), 5.0), 900.0),
                        "artifact": True,
                    },
                    {
                        "id": "wait_for_target_state",
                        "type": "game_feature_wait",
                        "plugin_name": plugin_name,
                        "expected_active": bool(activate),
                        "run_data_validation": bool(run_data_validation),
                        "risk": "ReadOnly",
                        "timeout": min(max(float(timeout), 5.0), 900.0),
                        "artifact": True,
                    },
                ],
            },
            endpoint=endpoint,
            project=project,
            token=token,
        )
        if result.get("success"):
            result = dict(result)
            result.update(
                game_feature=True,
                plugin_name=plugin_name,
                target_active=bool(activate),
                save_behavior="Never",
            )
        return result
    except (KeyError, TypeError, ValueError) as exc:
        return {
            "success": False,
            "phase": "game_feature",
            "retryable": False,
            "error": str(exc),
        }


_MATERIAL_GRAPH_OPS = {
    "add",
    "comment",
    "reroute",
    "connect",
    "connect_out",
    "disconnect_in",
    "disconnect_out",
    "set_prop",
    "delete",
}

_MATERIAL_GRAPH_FIELDS = {
    "op",
    "class_name",
    "x",
    "y",
    "src_ref",
    "src_output",
    "dst_ref",
    "dst_input",
    "property",
    "value",
    "width",
    "height",
    "text",
    "color",
}


def _material_package_path(material_path: str) -> str:
    path = str(material_path).strip()
    if not path.startswith("/"):
        raise ValueError("material_path must be an Unreal object/package path")
    package = path.split(".", 1)[0]
    if package.endswith("/") or package.count("/") < 2:
        raise ValueError("material_path does not identify an asset")
    return package


def _validate_material_graph_ops(ops: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
    if not isinstance(ops, list) or not 1 <= len(ops) <= 256:
        raise ValueError("ops must contain between 1 and 256 objects")
    normalized: List[Dict[str, Any]] = []
    for index, raw in enumerate(ops):
        if not isinstance(raw, dict):
            raise ValueError(f"ops[{index}] must be an object")
        unknown = sorted(set(raw) - _MATERIAL_GRAPH_FIELDS)
        if unknown:
            raise ValueError(f"ops[{index}] has unknown field(s): {', '.join(unknown)}")
        op_name = str(raw.get("op", "")).strip().lower()
        if op_name not in _MATERIAL_GRAPH_OPS:
            raise ValueError(
                f"ops[{index}].op must be one of {sorted(_MATERIAL_GRAPH_OPS)}"
            )
        item = dict(raw)
        item["op"] = op_name
        for field in ("x", "y", "width", "height"):
            if field in item and not isinstance(item[field], int):
                raise ValueError(f"ops[{index}].{field} must be an integer")
        color = item.get("color")
        if color is not None:
            if isinstance(color, dict):
                color = [
                    color.get("r", 0.2),
                    color.get("g", 0.7),
                    color.get("b", 1.0),
                    color.get("a", 0.4),
                ]
            if not isinstance(color, (list, tuple)) or len(color) not in (3, 4):
                raise ValueError(
                    f"ops[{index}].color must be [r,g,b] or [r,g,b,a]"
                )
            if not all(isinstance(component, (int, float)) for component in color):
                raise ValueError(f"ops[{index}].color components must be numbers")
            item["color"] = list(color) + ([0.4] if len(color) == 3 else [])
        normalized.append(item)
    return normalized


def _material_graph_intent_code(payload: Dict[str, Any]) -> str:
    encoded = base64.b64encode(
        json.dumps(payload, ensure_ascii=False, sort_keys=True).encode("utf-8")
    ).decode("ascii")
    return textwrap.dedent(
        f"""
        import base64
        import json
        import unreal

        _p = json.loads(base64.b64decode({encoded!r}).decode("utf-8"))
        _lib = unreal.UnrealBridgeMaterialLibrary
        _cs_lib = unreal.UnrealBridgeChangeSetLibrary

        def _attr(value, name, default=None):
            try:
                return getattr(value, name)
            except Exception:
                return default

        def _change_set(value):
            if value is None:
                return None
            return {{
                "change_set_id": str(_attr(value, "change_set_id", "")),
                "job_id": str(_attr(value, "job_id", "")),
                "name": str(_attr(value, "name", "")),
                "status": str(_attr(value, "status", "")),
                "success": bool(_attr(value, "success", False)),
                "can_commit": bool(_attr(value, "can_commit", False)),
                "saved": bool(_attr(value, "saved", False)),
                "target_packages": list(_attr(value, "target_packages", []) or []),
                "target_packages_dirty_at_begin": list(_attr(value, "target_packages_dirty_at_begin", []) or []),
                "dirty_packages_for_job": list(_attr(value, "dirty_packages_for_job", []) or []),
                "protected_preexisting_dirty_packages": list(_attr(value, "protected_preexisting_dirty_packages", []) or []),
                "unexpected_dirty_packages": list(_attr(value, "unexpected_dirty_packages", []) or []),
                "created_assets_for_job": list(_attr(value, "created_assets_for_job", []) or []),
                "removed_created_assets_during_rollback": list(_attr(value, "removed_created_assets_during_rollback", []) or []),
                "captured_objects_for_undo": int(_attr(value, "captured_objects_for_undo", 0) or 0),
                "reload_packages_on_rollback": list(_attr(value, "reload_packages_on_rollback", []) or []),
                "reloaded_packages_during_rollback": list(_attr(value, "reloaded_packages_during_rollback", []) or []),
                "rollback_verified": bool(_attr(value, "rollback_verified", False)),
                "error": str(_attr(value, "error", "")),
            }}

        def _finding(value):
            return {{
                "rule_id": str(_attr(value, "rule_id", "")),
                "severity": str(_attr(value, "severity", "")),
                "message": str(_attr(value, "message", "")),
                "expression_guid": str(_attr(value, "expression_guid", "")),
                "expression_class": str(_attr(value, "expression_class", "")),
                "detail": str(_attr(value, "detail", "")),
            }}

        def _analysis(value):
            return {{
                "found": bool(_attr(value, "found", False)),
                "path": str(_attr(value, "path", "")),
                "material_domain": str(_attr(value, "material_domain", "")),
                "shading_models": list(_attr(value, "shading_models", []) or []),
                "max_instructions": int(_attr(value, "max_instructions", 0) or 0),
                "sampler_count": int(_attr(value, "sampler_count", 0) or 0),
                "expression_count": int(_attr(value, "expression_count", 0) or 0),
                "compile_errors": list(_attr(value, "compile_errors", []) or []),
                "shader_stats_ready": bool(_attr(value, "shader_stats_ready", False)),
                "findings": [_finding(item) for item in (_attr(value, "findings", []) or [])],
            }}

        def _shader_status(value):
            return {{
                "found": bool(_attr(value, "found", False)),
                "shader_map_ready": bool(_attr(value, "shader_map_ready", False)),
                "pending_assets_global": int(_attr(value, "pending_assets_global", 0) or 0),
                "feature_level": str(_attr(value, "feature_level", "")),
                "quality_level": str(_attr(value, "quality_level", "")),
                "error": str(_attr(value, "error", "")),
            }}

        _ops = []
        for _raw in _p["ops"]:
            _op = unreal.BridgeMaterialGraphOp()
            for _field in (
                "op", "class_name", "x", "y", "src_ref", "src_output",
                "dst_ref", "dst_input", "property", "value", "width",
                "height", "text"
            ):
                if _field in _raw:
                    _op.set_editor_property(_field, _raw[_field])
            if "color" in _raw:
                _c = _raw["color"]
                _op.set_editor_property(
                    "color", unreal.LinearColor(float(_c[0]), float(_c[1]), float(_c[2]), float(_c[3]))
                )
            _ops.append(_op)

        _change_set_id = _cs_lib.begin_change_set(
            "UnrealBridge 3.0 Material Graph Intent", [_p["target_package"]]
        )
        _out = {{
            "success": False,
            "material_path": _p["material_path"],
            "target_package": _p["target_package"],
            "apply": bool(_p["apply"]),
            "create_if_missing": bool(_p["create_if_missing"]),
            "created_for_intent": False,
            "save_behavior": "Never",
            "change_set_id": str(_change_set_id),
            "rollback_required": False,
        }}
        try:
            if not unreal.EditorAssetLibrary.does_asset_exist(_p["material_path"]):
                if not _p["create_if_missing"]:
                    raise RuntimeError("material asset does not exist and create_if_missing is false")
                _asset_name = _p["target_package"].rsplit("/", 1)[-1]
                _folder = _p["target_package"].rsplit("/", 1)[0]
                _created = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                    _asset_name,
                    _folder,
                    unreal.Material,
                    unreal.MaterialFactoryNew(),
                )
                if _created is None:
                    raise RuntimeError("MaterialFactoryNew failed to create the requested asset")
                _out["created_for_intent"] = True
            _before = _lib.snapshot_material_graph_json(_p["material_path"])
            _result = _lib.apply_material_graph_ops(
                _p["material_path"], _ops, bool(_p["compile"])
            )
            _after = _lib.snapshot_material_graph_json(_p["material_path"])
            _analysis_value = _lib.analyze_material(
                _p["material_path"],
                int(_p["instruction_budget"]),
                int(_p["sampler_budget"]),
            )
            _status_value = _lib.get_material_shader_compile_status(
                _p["material_path"], _p["feature_level"], _p["quality"]
            )
            _op_success = bool(_attr(_result, "success", False))
            _out.update({{
                "ops_applied": int(_attr(_result, "ops_applied", 0) or 0),
                "failed_at_index": int(_attr(_result, "failed_at_index", -1)),
                "error": str(_attr(_result, "error", "")),
                "guids": [str(value) for value in (_attr(_result, "guids", []) or [])],
                "before_snapshot": json.loads(_before) if _before else {{}},
                "after_snapshot": json.loads(_after) if _after else {{}},
                "graph_diff": _lib.diff_material_graph_snapshots(_before, _after),
                "compile_errors": list(_lib.get_material_compile_errors(
                    _p["material_path"], _p["feature_level"], _p["quality"]
                ) or []),
                "shader_status": _shader_status(_status_value),
                "analysis": _analysis(_analysis_value),
                "preview": _change_set(_cs_lib.preview_change_set(_change_set_id)),
            }})
            if not _op_success:
                _rolled = _cs_lib.finalize_change_set(_change_set_id, False)
                _out["final_change_set"] = _change_set(_rolled)
                _out["success"] = False
            elif _p["apply"]:
                _committed = _cs_lib.finalize_change_set(_change_set_id, True)
                _out["final_change_set"] = _change_set(_committed)
                _out["success"] = bool(_attr(_committed, "success", False))
                _out["rollback_required"] = bool(_out["success"])
            else:
                _rolled = _cs_lib.finalize_change_set(_change_set_id, False)
                _out["final_change_set"] = _change_set(_rolled)
                _out["success"] = bool(_attr(_rolled, "success", False))
                _out["preview_rolled_back"] = bool(_out["success"])
                _asset_still_exists = unreal.EditorAssetLibrary.does_asset_exist(
                    _p["material_path"]
                )
                _out["asset_exists_after_rollback"] = bool(_asset_still_exists)
                if _asset_still_exists:
                    _restored = _lib.snapshot_material_graph_json(_p["material_path"])
                    _out["restored_snapshot"] = json.loads(_restored) if _restored else {{}}
                    _out["restored_diff"] = _lib.diff_material_graph_snapshots(
                        _before, _restored
                    )
                else:
                    _out["restored_snapshot"] = {{}}
                    _out["restored_diff"] = "asset removed by ChangeSet rollback"
        except Exception as _exc:
            try:
                _rolled = _cs_lib.finalize_change_set(_change_set_id, False)
                _out["final_change_set"] = _change_set(_rolled)
            except Exception as _rollback_exc:
                _out["rollback_error"] = str(_rollback_exc)
            _out["error"] = str(_exc)
            _out["success"] = False
        print(json.dumps(_out, ensure_ascii=False))
        """
    ).strip()


@mcp.tool()
def bridge_submit_material_graph_intent(
    material_path: str,
    ops: List[Dict[str, Any]],
    create_if_missing: bool = False,
    apply: bool = False,
    compile: bool = True,
    instruction_budget: int = 0,
    sampler_budget: int = 0,
    feature_level: str = "SM6",
    quality: str = "High",
    idempotency_key: Optional[str] = None,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
) -> Dict[str, Any]:
    """Preview/apply a typed Material graph intent in one unsaved ChangeSet.

    ``apply=false`` (the default) captures graph before/after, compile/lint,
    shader status, and ChangeSet dirty ownership, then rolls the edit back.
    ``apply=true`` commits only to the editor undo buffer and still never saves;
    the returned scenario can roll the committed ChangeSet back if a later step
    fails.
    """
    try:
        path = str(material_path).strip()
        package = _material_package_path(path)
        normalized_ops = _validate_material_graph_ops(ops)
        if int(instruction_budget) < 0 or int(sampler_budget) < 0:
            raise ValueError("instruction_budget and sampler_budget must be >= 0")
        if str(feature_level) not in {"Default", "SM5", "SM6", "ES3_1"}:
            raise ValueError("feature_level must be Default, SM5, SM6, or ES3_1")
        if str(quality) not in {"Default", "Low", "Medium", "High", "Epic"}:
            raise ValueError("quality must be Default, Low, Medium, High, or Epic")
        payload = {
            "material_path": path,
            "target_package": package,
            "ops": normalized_ops,
            "create_if_missing": bool(create_if_missing),
            "apply": bool(apply),
            "compile": bool(compile),
            "instruction_budget": int(instruction_budget),
            "sampler_budget": int(sampler_budget),
            "feature_level": str(feature_level),
            "quality": str(quality),
        }
        stable = hashlib.sha256(
            json.dumps(payload, ensure_ascii=False, sort_keys=True).encode("utf-8")
        ).hexdigest()[:32]
        return bridge_submit_scenario(
            {
                "name": f"Material 3.0 graph intent: {path}",
                "metadata": {
                    "kind": "material_graph_intent",
                    "material_path": path,
                    "target_package": package,
                    "apply": bool(apply),
                    "save_behavior": "Never",
                },
                "steps": [
                    {
                        "id": "material_graph_intent",
                        "type": "job",
                        "code": _material_graph_intent_code(payload),
                        "risk": "TransactionalSync",
                        "idempotency_key": idempotency_key or f"material:{stable}",
                        "queue_timeout": 300.0,
                        "run_timeout": 900.0,
                        "timeout": 900.0,
                        "no_preflight": True,
                        "artifact": True,
                        "preview": not bool(apply),
                    }
                ],
            },
            endpoint=endpoint,
            project=project,
            token=token,
        )
    except (TypeError, ValueError) as exc:
        return {
            "success": False,
            "phase": "material",
            "retryable": False,
            "error": str(exc),
        }


_METASOUND_GRAPH_OPS = {
    "add_input",
    "add_output",
    "remove_input",
    "remove_output",
    "add_node",
    "connect",
    "disconnect_input",
    "disconnect_output",
    "set_default",
    "remove_node",
    "set_location",
}

_METASOUND_GRAPH_FIELDS = {
    "op",
    "name",
    "data_type",
    "literal_type",
    "value",
    "namespace",
    "class_name",
    "variant",
    "major_version",
    "src_ref",
    "src_output",
    "dst_ref",
    "dst_input",
    "x",
    "y",
}


def _metasound_package_path(metasound_path: str) -> str:
    path = str(metasound_path).strip()
    if not path.startswith("/"):
        raise ValueError("metasound_path must be an Unreal object/package path")
    package = path.split(".", 1)[0]
    if package.endswith("/") or package.count("/") < 2:
        raise ValueError("metasound_path does not identify an asset")
    return package


def _validate_metasound_graph_ops(
    ops: List[Dict[str, Any]],
) -> List[Dict[str, Any]]:
    if not isinstance(ops, list) or not 1 <= len(ops) <= 256:
        raise ValueError("ops must contain between 1 and 256 objects")
    normalized: List[Dict[str, Any]] = []
    required = {
        "add_input": {"name", "data_type"},
        "add_output": {"name", "data_type"},
        "remove_input": {"name"},
        "remove_output": {"name"},
        "add_node": {"namespace", "class_name"},
        "connect": {"src_ref", "src_output", "dst_ref", "dst_input"},
        "disconnect_input": {"dst_ref", "dst_input"},
        "disconnect_output": {"src_ref", "src_output"},
        "set_default": {"dst_ref", "dst_input", "literal_type", "value"},
        "remove_node": {"dst_ref"},
        "set_location": {"dst_ref", "x", "y"},
    }
    literal_types = {"Default", "Bool", "Int", "Float", "String", "Object"}
    for index, raw in enumerate(ops):
        if not isinstance(raw, dict):
            raise ValueError(f"ops[{index}] must be an object")
        unknown = sorted(set(raw) - _METASOUND_GRAPH_FIELDS)
        if unknown:
            raise ValueError(f"ops[{index}] has unknown field(s): {', '.join(unknown)}")
        op_name = str(raw.get("op", "")).strip().lower()
        if op_name not in _METASOUND_GRAPH_OPS:
            raise ValueError(
                f"ops[{index}].op must be one of {sorted(_METASOUND_GRAPH_OPS)}"
            )
        missing = [
            field
            for field in sorted(required[op_name])
            if field not in raw or raw[field] in (None, "")
        ]
        if missing:
            raise ValueError(
                f"ops[{index}] ({op_name}) is missing: {', '.join(missing)}"
            )
        item = dict(raw)
        item["op"] = op_name
        if "major_version" in item:
            if not isinstance(item["major_version"], int) or item["major_version"] < 1:
                raise ValueError(f"ops[{index}].major_version must be an integer >= 1")
        for field in ("x", "y"):
            if field in item and not isinstance(item[field], (int, float)):
                raise ValueError(f"ops[{index}].{field} must be a number")
        if "literal_type" in item:
            literal_type = str(item["literal_type"])
            if literal_type not in literal_types:
                raise ValueError(
                    f"ops[{index}].literal_type must be one of {sorted(literal_types)}"
                )
        for field, value in item.items():
            if field not in {"op", "major_version", "x", "y"} and not isinstance(value, str):
                raise ValueError(f"ops[{index}].{field} must be a string")
        normalized.append(item)
    return normalized


def _metasound_graph_intent_code(payload: Dict[str, Any]) -> str:
    encoded = base64.b64encode(
        json.dumps(payload, ensure_ascii=False, sort_keys=True).encode("utf-8")
    ).decode("ascii")
    return textwrap.dedent(
        f"""
        import base64
        import json
        import unreal

        _p = json.loads(base64.b64decode({encoded!r}).decode("utf-8"))
        _lib = unreal.UnrealBridgeAudioLibrary
        _cs_lib = unreal.UnrealBridgeChangeSetLibrary

        def _attr(value, name, default=None):
            try:
                return getattr(value, name)
            except Exception:
                return default

        def _change_set(value):
            if value is None:
                return None
            return {{
                "change_set_id": str(_attr(value, "change_set_id", "")),
                "job_id": str(_attr(value, "job_id", "")),
                "name": str(_attr(value, "name", "")),
                "status": str(_attr(value, "status", "")),
                "success": bool(_attr(value, "success", False)),
                "can_commit": bool(_attr(value, "can_commit", False)),
                "saved": bool(_attr(value, "saved", False)),
                "target_packages": list(_attr(value, "target_packages", []) or []),
                "target_packages_dirty_at_begin": list(_attr(value, "target_packages_dirty_at_begin", []) or []),
                "dirty_packages_for_job": list(_attr(value, "dirty_packages_for_job", []) or []),
                "protected_preexisting_dirty_packages": list(_attr(value, "protected_preexisting_dirty_packages", []) or []),
                "unexpected_dirty_packages": list(_attr(value, "unexpected_dirty_packages", []) or []),
                "created_assets_for_job": list(_attr(value, "created_assets_for_job", []) or []),
                "removed_created_assets_during_rollback": list(_attr(value, "removed_created_assets_during_rollback", []) or []),
                "captured_objects_for_undo": int(_attr(value, "captured_objects_for_undo", 0) or 0),
                "reload_packages_on_rollback": list(_attr(value, "reload_packages_on_rollback", []) or []),
                "reloaded_packages_during_rollback": list(_attr(value, "reloaded_packages_during_rollback", []) or []),
                "rollback_verified": bool(_attr(value, "rollback_verified", False)),
                "error": str(_attr(value, "error", "")),
            }}

        def _member(value):
            return {{
                "name": str(_attr(value, "name", "")),
                "data_type": str(_attr(value, "data_type", "")),
                "access_type": str(_attr(value, "access_type", "")),
                "default_value": str(_attr(value, "default_value", "")),
            }}

        def _page(value):
            return {{
                "page_id": str(_attr(value, "page_id", "")),
                "node_count": int(_attr(value, "node_count", 0) or 0),
                "edge_count": int(_attr(value, "edge_count", 0) or 0),
                "variable_count": int(_attr(value, "variable_count", 0) or 0),
            }}

        def _node(value):
            return {{
                "node_id": str(_attr(value, "node_id", "")),
                "name": str(_attr(value, "name", "")),
                "class_id": str(_attr(value, "class_id", "")),
                "class_name": str(_attr(value, "class_name", "")),
                "page_id": str(_attr(value, "page_id", "")),
                "input_count": int(_attr(value, "input_count", 0) or 0),
                "output_count": int(_attr(value, "output_count", 0) or 0),
            }}

        def _graph(value):
            return {{
                "found": bool(_attr(value, "found", False)),
                "path": str(_attr(value, "path", "")),
                "asset_class": str(_attr(value, "asset_class", "")),
                "root_class_name": str(_attr(value, "root_class_name", "")),
                "root_class_version": str(_attr(value, "root_class_version", "")),
                "document_version": str(_attr(value, "document_version", "")),
                "template_type": str(_attr(value, "template_type", "")),
                "preset": bool(_attr(value, "preset", False)),
                "total_page_count": int(_attr(value, "total_page_count", 0) or 0),
                "total_node_count": int(_attr(value, "total_node_count", 0) or 0),
                "total_edge_count": int(_attr(value, "total_edge_count", 0) or 0),
                "dependency_count": int(_attr(value, "dependency_count", 0) or 0),
                "interfaces": list(_attr(value, "interfaces", []) or []),
                "inputs": [_member(item) for item in (_attr(value, "inputs", []) or [])],
                "outputs": [_member(item) for item in (_attr(value, "outputs", []) or [])],
                "pages": [_page(item) for item in (_attr(value, "pages", []) or [])],
                "nodes": [_node(item) for item in (_attr(value, "nodes", []) or [])],
                "validation_result": str(_attr(value, "validation_result", "")),
                "validation_messages": list(_attr(value, "validation_messages", []) or []),
                "error": str(_attr(value, "error", "")),
            }}

        def _names(items):
            return sorted(str(item.get("name", "")) for item in items)

        def _node_ids(items):
            return sorted(str(item.get("node_id", "")) for item in items)

        def _diff(before, after):
            before_nodes = set(_node_ids(before.get("nodes", [])))
            after_nodes = set(_node_ids(after.get("nodes", [])))
            before_inputs = set(_names(before.get("inputs", [])))
            after_inputs = set(_names(after.get("inputs", [])))
            before_outputs = set(_names(before.get("outputs", [])))
            after_outputs = set(_names(after.get("outputs", [])))
            return {{
                "node_delta": after.get("total_node_count", 0) - before.get("total_node_count", 0),
                "edge_delta": after.get("total_edge_count", 0) - before.get("total_edge_count", 0),
                "added_node_ids": sorted(after_nodes - before_nodes),
                "removed_node_ids": sorted(before_nodes - after_nodes),
                "added_inputs": sorted(after_inputs - before_inputs),
                "removed_inputs": sorted(before_inputs - after_inputs),
                "added_outputs": sorted(after_outputs - before_outputs),
                "removed_outputs": sorted(before_outputs - after_outputs),
                "validation_changed": before.get("validation_result") != after.get("validation_result"),
            }}

        _ops = []
        for _raw in _p["ops"]:
            _op = unreal.BridgeMetaSoundGraphOp()
            for _field in (
                "op", "name", "data_type", "literal_type", "value",
                "namespace", "class_name", "variant", "major_version",
                "src_ref", "src_output", "dst_ref", "dst_input", "x", "y"
            ):
                if _field in _raw:
                    _op.set_editor_property(_field, _raw[_field])
            _ops.append(_op)

        _change_set_id = _cs_lib.begin_change_set(
            "UnrealBridge 3.0 MetaSound Graph Intent", [_p["target_package"]]
        )
        _out = {{
            "success": False,
            "metasound_path": _p["metasound_path"],
            "target_package": _p["target_package"],
            "apply": bool(_p["apply"]),
            "template_path": _p["template_path"],
            "created_for_intent": False,
            "save_behavior": "Never",
            "change_set_id": str(_change_set_id),
            "rollback_required": False,
        }}
        try:
            if not unreal.EditorAssetLibrary.does_asset_exist(_p["metasound_path"]):
                if not _p["template_path"]:
                    raise RuntimeError("MetaSound asset does not exist and template_path is empty")
                _created = unreal.EditorAssetLibrary.duplicate_asset(
                    _p["template_path"], _p["target_package"]
                )
                if _created is None:
                    raise RuntimeError("failed to duplicate the MetaSound template")
                _out["created_for_intent"] = True
            _before = _graph(_lib.get_meta_sound_graph_info(
                _p["metasound_path"], int(_p["max_nodes"])
            ))
            _result = _lib.apply_meta_sound_graph_ops(
                _p["metasound_path"], _ops, bool(_p["register_frontend"])
            )
            _after = _graph(_lib.get_meta_sound_graph_info(
                _p["metasound_path"], int(_p["max_nodes"])
            ))
            _op_success = bool(_attr(_result, "success", False))
            _out.update({{
                "ops_applied": int(_attr(_result, "ops_applied", 0) or 0),
                "failed_at_index": int(_attr(_result, "failed_at_index", -1)),
                "produced_node_ids": [str(value) for value in (_attr(_result, "produced_node_ids", []) or [])],
                "registered_with_frontend": bool(_attr(_result, "registered_with_frontend", False)),
                "validation_result": str(_attr(_result, "validation_result", "")),
                "validation_messages": list(_attr(_result, "validation_messages", []) or []),
                "error": str(_attr(_result, "error", "")),
                "before_graph": _before,
                "after_graph": _after,
                "graph_diff": _diff(_before, _after),
                "preview": _change_set(_cs_lib.preview_change_set(_change_set_id)),
            }})
            if not _op_success:
                _rolled = _cs_lib.finalize_change_set(_change_set_id, False)
                _out["final_change_set"] = _change_set(_rolled)
                _out["success"] = False
            elif _p["apply"]:
                _committed = _cs_lib.finalize_change_set(_change_set_id, True)
                _out["final_change_set"] = _change_set(_committed)
                _out["success"] = bool(_attr(_committed, "success", False))
                _out["rollback_required"] = bool(_out["success"])
            else:
                _rolled = _cs_lib.finalize_change_set(_change_set_id, False)
                _out["final_change_set"] = _change_set(_rolled)
                _out["success"] = bool(_attr(_rolled, "success", False))
                _out["preview_rolled_back"] = bool(_out["success"])
                _asset_still_exists = unreal.EditorAssetLibrary.does_asset_exist(
                    _p["metasound_path"]
                )
                _out["asset_exists_after_rollback"] = bool(_asset_still_exists)
                if _asset_still_exists:
                    _out["restored_graph"] = _graph(_lib.get_meta_sound_graph_info(
                        _p["metasound_path"], int(_p["max_nodes"])
                    ))
                else:
                    _out["restored_graph"] = {{}}
        except Exception as _exc:
            try:
                _rolled = _cs_lib.finalize_change_set(_change_set_id, False)
                _out["final_change_set"] = _change_set(_rolled)
            except Exception as _rollback_exc:
                _out["rollback_error"] = str(_rollback_exc)
            _out["error"] = str(_exc)
            _out["success"] = False
        print(json.dumps(_out, ensure_ascii=False))
        """
    ).strip()


@mcp.tool()
def bridge_submit_metasound_graph_intent(
    metasound_path: str,
    ops: List[Dict[str, Any]],
    template_path: Optional[str] = None,
    apply: bool = False,
    register_frontend: bool = True,
    max_nodes: int = 4096,
    idempotency_key: Optional[str] = None,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
) -> Dict[str, Any]:
    """Preview/apply typed MetaSound Builder operations in one unsaved ChangeSet.

    The default preview captures the graph before/after, structural diff, data
    validation, frontend registration result, and dirty ownership before rolling
    everything back. ``apply=true`` commits only to the editor undo buffer and
    still never saves the package.
    """
    try:
        path = str(metasound_path).strip()
        package = _metasound_package_path(path)
        template = str(template_path or "").strip()
        if template and not template.startswith("/"):
            raise ValueError("template_path must be an Unreal object/package path")
        normalized_ops = _validate_metasound_graph_ops(ops)
        if not 1 <= int(max_nodes) <= 65536:
            raise ValueError("max_nodes must be between 1 and 65536")
        payload = {
            "metasound_path": path,
            "target_package": package,
            "ops": normalized_ops,
            "template_path": template,
            "apply": bool(apply),
            "register_frontend": bool(register_frontend),
            "max_nodes": int(max_nodes),
        }
        stable = hashlib.sha256(
            json.dumps(payload, ensure_ascii=False, sort_keys=True).encode("utf-8")
        ).hexdigest()[:32]
        return bridge_submit_scenario(
            {
                "name": f"MetaSound 3.0 graph intent: {path}",
                "metadata": {
                    "kind": "metasound_graph_intent",
                    "metasound_path": path,
                    "target_package": package,
                    "apply": bool(apply),
                    "save_behavior": "Never",
                },
                "steps": [
                    {
                        "id": "metasound_graph_intent",
                        "type": "job",
                        "code": _metasound_graph_intent_code(payload),
                        "risk": "TransactionalSync",
                        "idempotency_key": idempotency_key or f"metasound:{stable}",
                        "queue_timeout": 300.0,
                        "run_timeout": 900.0,
                        "timeout": 900.0,
                        "no_preflight": True,
                        "artifact": True,
                        "preview": not bool(apply),
                    }
                ],
            },
            endpoint=endpoint,
            project=project,
            token=token,
        )
    except (TypeError, ValueError) as exc:
        return {
            "success": False,
            "phase": "metasound",
            "retryable": False,
            "error": str(exc),
        }


def _niagara_failure(exc: Exception) -> Dict[str, Any]:
    return {
        "success": False,
        "phase": "niagara",
        "retryable": False,
        "error": str(exc),
    }


def _niagara_target_list(
    system_path: str, target_packages: Optional[List[str]]
) -> List[str]:
    derived = package_from_object_path(system_path)
    result = [derived]
    for item in target_packages or []:
        package = package_from_object_path(str(item))
        if package not in result:
            result.append(package)
    return result


def _niagara_stack_ref_from_request(
    system_path: str, request: Dict[str, Any]
) -> Dict[str, Any]:
    existing = request.get("stack_input_ref") or request.get("stackInputRef")
    if isinstance(existing, dict):
        result = dict(existing)
        system = result.get("system")
        if not isinstance(system, dict) or system.get("refPath") != system_path:
            raise ValueError(
                "stack_input_ref.system.refPath must exactly match system_path"
            )
        return result
    return stack_item_reference(
        system_path,
        emitter_name=str(request.get("emitter_name") or ""),
        script_name=str(request.get("script_name") or ""),
        module_name=str(request.get("module_name") or ""),
        renderer_index=int(request.get("renderer_index", -1)),
        input_name_stack=list(request.get("input_name_stack") or []),
    )


def _niagara_emitter_guard_step(
    step_id: str,
    system_path: str,
    emitter_name: str,
    emitter_handle_id: str,
) -> Dict[str, Any]:
    if not emitter_name or not emitter_handle_id:
        raise ValueError(
            "mutating emitter inputs requires exact emitter_name and emitter_handle_id"
        )
    return {
        "id": step_id,
        "type": "exec",
        "code": emitter_identity_guard_code(
            system_path, emitter_name, emitter_handle_id
        ),
        "risk": "ReadOnly",
        "no_preflight": True,
        "timeout": 60.0,
        "artifact": True,
    }


def _niagara_read_step(
    step_id: str, operation: str, arguments: Dict[str, Any]
) -> Dict[str, Any]:
    return {
        "id": step_id,
        "type": "domain",
        "domain": "niagara",
        "operation": operation,
        "arguments": arguments,
        "timeout": 180.0,
        "run_timeout": 180.0,
        "artifact": True,
    }


def _submit_niagara_mutation_scenario(
    *,
    interface_name: str,
    system_path: str,
    calls: List[Dict[str, Any]],
    target_packages: Optional[List[str]],
    apply: bool,
    pre_steps: Optional[List[Dict[str, Any]]] = None,
    readback_refs: Optional[List[Dict[str, Any]]] = None,
    extra_read_steps: Optional[List[Dict[str, Any]]] = None,
    user_parameter_renames: Optional[List[Dict[str, str]]] = None,
    endpoint: Optional[str],
    project: Optional[str],
    token: Optional[str],
) -> Dict[str, Any]:
    if not calls and not user_parameter_renames:
        raise ValueError(
            "Niagara mutation workflow requires at least one call or rename"
        )
    steps = list(pre_steps or [])
    transactional_calls = list(calls)
    for stack_ref in readback_refs or []:
        transactional_calls.append(
            {
                "domain": "niagara",
                "operation": "GetStackInputData",
                "arguments": {"stackInputRef": stack_ref},
            }
        )
    for read_step in extra_read_steps or []:
        if read_step.get("type") != "domain":
            raise ValueError(
                "Niagara transactional readbacks must be domain steps"
            )
        transactional_calls.append(
            {
                "domain": str(read_step.get("domain") or "niagara"),
                "operation": str(read_step.get("operation") or ""),
                "arguments": dict(read_step.get("arguments") or {}),
            }
        )
    steps.append(
        {
            "id": "mutate",
            "type": "official_transactional_batch",
            "calls": transactional_calls,
            "target_packages": _niagara_target_list(
                system_path, target_packages
            ),
            "apply": bool(apply),
            "allow_readbacks": True,
            "compile_niagara_system_path": system_path,
            "niagara_user_parameter_renames": list(
                user_parameter_renames or []
            ),
            "timeout": 300.0,
            "artifact": True,
        }
    )
    result = bridge_submit_scenario(
        {
            "name": f"Niagara 3.0: {interface_name}",
            "metadata": {
                "kind": "niagara",
                "interface": interface_name,
                "system_path": system_path,
                "apply": bool(apply),
                "save_behavior": "Never",
                "transaction_count": 1,
            },
            "steps": steps,
        },
        endpoint=endpoint,
        project=project,
        token=token,
    )
    if result.get("success"):
        result = dict(result)
        result.update(
            niagara_interface=interface_name,
            apply=bool(apply),
            save_behavior="Never",
            transaction_count=1,
        )
    return result


@mcp.tool()
def rename_niagara_user_parameter(
    system_path: str,
    old_name: str,
    new_name: str,
    target_packages: Optional[List[str]] = None,
    apply: bool = False,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
) -> Dict[str, Any]:
    """Rename one User.* parameter and every graph reference in one ChangeSet."""
    try:
        old_value = str(old_name).strip()
        new_value = str(new_name).strip()
        if not old_value.startswith("User.") or not new_value.startswith("User."):
            raise ValueError("old_name and new_name must both use the User namespace")
        if old_value == new_value:
            raise ValueError("old_name and new_name must be different")
        return _submit_niagara_mutation_scenario(
            interface_name="rename_niagara_user_parameter",
            system_path=system_path,
            calls=[],
            target_packages=target_packages,
            apply=apply,
            user_parameter_renames=[
                {"old_name": old_value, "new_name": new_value}
            ],
            extra_read_steps=[
                _niagara_read_step(
                    "user_parameters",
                    "epic:NiagaraToolsets.NiagaraToolset_System.GetUserVariables",
                    {"system": object_ref(system_path)},
                )
            ],
            endpoint=endpoint,
            project=project,
            token=token,
        )
    except (KeyError, TypeError, ValueError) as exc:
        return _niagara_failure(exc)


@mcp.tool()
def get_niagara_module_inputs(
    system_path: str,
    emitter_name: str,
    script_name: str,
    module_name: str,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
    timeout: float = 15.0,
) -> Dict[str, Any]:
    """Return a Job for resolved values of every exact input on one Niagara module."""
    try:
        module_ref = stack_item_reference(
            system_path,
            emitter_name=emitter_name,
            script_name=script_name,
            module_name=module_name,
        )
        return bridge_call_domain_operation(
            "niagara",
            "GetModuleInputValues",
            {"moduleRef": module_ref},
            endpoint=endpoint,
            project=project,
            token=token,
            timeout=timeout,
        )
    except ValueError as exc:
        return _niagara_failure(exc)


@mcp.tool()
def get_niagara_input_binding_tree(
    system_path: str,
    emitter_name: str,
    script_name: str,
    module_name: str,
    input_name_stack: List[str],
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
) -> Dict[str, Any]:
    """Persistently inspect topology, current value, and any nested dynamic-input chain."""
    try:
        stack_ref = stack_item_reference(
            system_path,
            emitter_name=emitter_name,
            script_name=script_name,
            module_name=module_name,
            input_name_stack=input_name_stack,
        )
        return bridge_submit_scenario(
            {
                "name": "Niagara 3.0: get_niagara_input_binding_tree",
                "metadata": {
                    "kind": "niagara",
                    "interface": "get_niagara_input_binding_tree",
                    "system_path": system_path,
                },
                "steps": [
                    {
                        "id": "binding_tree",
                        "type": "niagara_binding_tree",
                        "stack_input_ref": stack_ref,
                        "timeout": 180.0,
                        "artifact": True,
                    }
                ],
            },
            endpoint=endpoint,
            project=project,
            token=token,
        )
    except ValueError as exc:
        return _niagara_failure(exc)


@mcp.tool()
def list_niagara_dynamic_input_scripts(
    type_ref_path: str,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
    timeout: float = 15.0,
) -> Dict[str, Any]:
    """Return a Job listing dynamic-input scripts compatible with an exact Niagara type."""
    try:
        return bridge_call_domain_operation(
            "niagara",
            "GetAvailableDynamicInputs",
            {"type": type_definition(type_ref_path)},
            endpoint=endpoint,
            project=project,
            token=token,
            timeout=timeout,
        )
    except ValueError as exc:
        return _niagara_failure(exc)


@mcp.tool()
def ensure_niagara_user_parameters(
    system_path: str,
    parameters: List[Dict[str, Any]],
    target_packages: Optional[List[str]] = None,
    apply: bool = False,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
) -> Dict[str, Any]:
    """Ensure typed user parameters in one preview-by-default, never-save transaction."""
    try:
        call = add_user_variables_call(system_path, parameters)
        result = _submit_niagara_mutation_scenario(
            interface_name="ensure_niagara_user_parameters",
            system_path=system_path,
            calls=[call],
            target_packages=target_packages,
            apply=apply,
            endpoint=endpoint,
            project=project,
            token=token,
        )
        return result
    except (KeyError, TypeError, ValueError) as exc:
        return _niagara_failure(exc)


@mcp.tool()
def rename_niagara_emitter(
    system_path: str,
    emitter_name: str,
    emitter_handle_id: str,
    new_name: str,
    target_packages: Optional[List[str]] = None,
    apply: bool = False,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
) -> Dict[str, Any]:
    """Rename one name+handle-id verified emitter, with summary and diagnostics readback."""
    try:
        pre_steps = [
            _niagara_emitter_guard_step(
                "verify_emitter",
                system_path,
                emitter_name,
                emitter_handle_id,
            )
        ]
        result = _submit_niagara_mutation_scenario(
            interface_name="rename_niagara_emitter",
            system_path=system_path,
            calls=[rename_emitter_call(system_path, emitter_name, new_name)],
            target_packages=target_packages,
            apply=apply,
            pre_steps=pre_steps,
            extra_read_steps=[
                _niagara_read_step(
                    "emitter_summary",
                    "GetSystemSummary",
                    {"system": object_ref(system_path)},
                )
            ],
            endpoint=endpoint,
            project=project,
            token=token,
        )
        return result
    except (KeyError, TypeError, ValueError) as exc:
        return _niagara_failure(exc)


def _single_niagara_input_workflow(
    *,
    interface_name: str,
    system_path: str,
    emitter_name: str,
    emitter_handle_id: str,
    script_name: str,
    module_name: str,
    input_name_stack: List[str],
    input_data: Dict[str, Any],
    replace_existing: bool,
    target_packages: Optional[List[str]],
    apply: bool,
    endpoint: Optional[str],
    project: Optional[str],
    token: Optional[str],
) -> Dict[str, Any]:
    stack_ref = stack_item_reference(
        system_path,
        emitter_name=emitter_name,
        script_name=script_name,
        module_name=module_name,
        input_name_stack=input_name_stack,
    )
    pre_steps = [
        _niagara_emitter_guard_step(
            "verify_emitter",
            system_path,
            emitter_name,
            emitter_handle_id,
        ),
        {
            "id": "guard_input",
            "type": "niagara_input_guard",
            "stack_input_ref": stack_ref,
            "replace_existing": bool(replace_existing),
            "timeout": 180.0,
            "artifact": True,
        },
    ]
    return _submit_niagara_mutation_scenario(
        interface_name=interface_name,
        system_path=system_path,
        calls=[set_stack_input_call(stack_ref, input_data)],
        target_packages=target_packages,
        apply=apply,
        pre_steps=pre_steps,
        readback_refs=[stack_ref],
        endpoint=endpoint,
        project=project,
        token=token,
    )


@mcp.tool()
def bind_niagara_module_input_to_user_parameter(
    system_path: str,
    emitter_name: str,
    emitter_handle_id: str,
    script_name: str,
    module_name: str,
    input_name_stack: List[str],
    user_parameter_name: str,
    type_ref_path: str,
    replace_existing: bool = False,
    target_packages: Optional[List[str]] = None,
    apply: bool = False,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
) -> Dict[str, Any]:
    """Bind an exact module input to User.*, refusing replacement unless opted in."""
    try:
        return _single_niagara_input_workflow(
            interface_name="bind_niagara_module_input_to_user_parameter",
            system_path=system_path,
            emitter_name=emitter_name,
            emitter_handle_id=emitter_handle_id,
            script_name=script_name,
            module_name=module_name,
            input_name_stack=input_name_stack,
            input_data=linked_input_data(user_parameter_name, type_ref_path),
            replace_existing=replace_existing,
            target_packages=target_packages,
            apply=apply,
            endpoint=endpoint,
            project=project,
            token=token,
        )
    except (KeyError, TypeError, ValueError) as exc:
        return _niagara_failure(exc)


@mcp.tool()
def attach_niagara_dynamic_input(
    system_path: str,
    emitter_name: str,
    emitter_handle_id: str,
    script_name: str,
    module_name: str,
    input_name_stack: List[str],
    dynamic_input_script_path: str,
    replace_existing: bool = False,
    target_packages: Optional[List[str]] = None,
    apply: bool = False,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
) -> Dict[str, Any]:
    """Attach a typed Niagara dynamic-input asset to an exact stack input."""
    try:
        return _single_niagara_input_workflow(
            interface_name="attach_niagara_dynamic_input",
            system_path=system_path,
            emitter_name=emitter_name,
            emitter_handle_id=emitter_handle_id,
            script_name=script_name,
            module_name=module_name,
            input_name_stack=input_name_stack,
            input_data=dynamic_input_data(dynamic_input_script_path),
            replace_existing=replace_existing,
            target_packages=target_packages,
            apply=apply,
            endpoint=endpoint,
            project=project,
            token=token,
        )
    except (KeyError, TypeError, ValueError) as exc:
        return _niagara_failure(exc)


@mcp.tool()
def set_niagara_dynamic_input_sub_input(
    system_path: str,
    emitter_name: str,
    emitter_handle_id: str,
    script_name: str,
    module_name: str,
    input_name_stack: List[str],
    input_struct_ref_path: str,
    value: Optional[Any] = None,
    replace_existing: bool = False,
    target_packages: Optional[List[str]] = None,
    apply: bool = False,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
) -> Dict[str, Any]:
    """Set one exact nested dynamic-input leaf using an explicit Unreal struct type."""
    try:
        return _single_niagara_input_workflow(
            interface_name="set_niagara_dynamic_input_sub_input",
            system_path=system_path,
            emitter_name=emitter_name,
            emitter_handle_id=emitter_handle_id,
            script_name=script_name,
            module_name=module_name,
            input_name_stack=input_name_stack,
            input_data=literal_input_data(input_struct_ref_path, value),
            replace_existing=replace_existing,
            target_packages=target_packages,
            apply=apply,
            endpoint=endpoint,
            project=project,
            token=token,
        )
    except (KeyError, TypeError, ValueError) as exc:
        return _niagara_failure(exc)


@mcp.tool()
def get_niagara_compile_diagnostics(
    system_path: str,
    wait_for_compile: bool = True,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
) -> Dict[str, Any]:
    """Persist compile state and complete stack issues as retained scenario artifacts."""
    try:
        object_ref(system_path)
        steps: List[Dict[str, Any]] = []
        if wait_for_compile:
            steps.append(
                {
                    "id": "wait_compile",
                    "type": "niagara_wait_compile",
                    "system_path": system_path,
                    "timeout": 300.0,
                    "artifact": True,
                }
            )
        steps.extend(
            [
                _niagara_read_step(
                    "compile_state",
                    "GetSystemCompileState",
                    {"system": object_ref(system_path)},
                ),
                _niagara_read_step(
                    "stack_issues",
                    "GetStackIssues",
                    {"system": object_ref(system_path)},
                ),
            ]
        )
        return bridge_submit_scenario(
            {
                "name": "Niagara 3.0: get_niagara_compile_diagnostics",
                "metadata": {
                    "kind": "niagara",
                    "interface": "get_niagara_compile_diagnostics",
                    "system_path": system_path,
                },
                "steps": steps,
            },
            endpoint=endpoint,
            project=project,
            token=token,
        )
    except ValueError as exc:
        return _niagara_failure(exc)


@mcp.tool()
def configure_niagara_particle_controls(
    system_path: str,
    parameters: Optional[List[Dict[str, Any]]] = None,
    bindings: Optional[List[Dict[str, Any]]] = None,
    replace_existing: bool = False,
    target_packages: Optional[List[str]] = None,
    apply: bool = False,
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
) -> Dict[str, Any]:
    """Atomically ensure controls and bind exact particle inputs, then read back and diagnose."""
    try:
        calls: List[Dict[str, Any]] = []
        if parameters:
            calls.append(add_user_variables_call(system_path, parameters))
        pre_steps: List[Dict[str, Any]] = []
        readback_refs: List[Dict[str, Any]] = []
        seen_emitters = set()
        for index, binding in enumerate(bindings or []):
            if not isinstance(binding, dict):
                raise ValueError(f"bindings[{index}] must be an object")
            stack_ref = _niagara_stack_ref_from_request(system_path, binding)
            emitter_name = str(stack_ref.get("emitterName") or "")
            emitter_handle_id = str(binding.get("emitter_handle_id") or "")
            emitter_key = (emitter_name.lower(), emitter_handle_id.lower())
            if emitter_key not in seen_emitters:
                pre_steps.append(
                    _niagara_emitter_guard_step(
                        f"verify_emitter_{len(seen_emitters)}",
                        system_path,
                        emitter_name,
                        emitter_handle_id,
                    )
                )
                seen_emitters.add(emitter_key)
            binding_replace = bool(
                binding.get("replace_existing", replace_existing)
            )
            pre_steps.append(
                {
                    "id": f"guard_input_{index}",
                    "type": "niagara_input_guard",
                    "stack_input_ref": stack_ref,
                    "replace_existing": binding_replace,
                    "timeout": 180.0,
                    "artifact": True,
                }
            )
            explicit_data = binding.get("input_data") or binding.get("inputData")
            modes = sum(
                bool(candidate)
                for candidate in (
                    explicit_data,
                    binding.get("user_parameter_name"),
                    binding.get("dynamic_input_script_path"),
                    binding.get("input_struct_ref_path"),
                )
            )
            if modes != 1:
                raise ValueError(
                    f"bindings[{index}] must select exactly one input_data, "
                    "user_parameter_name, dynamic_input_script_path, or "
                    "input_struct_ref_path"
                )
            if explicit_data:
                input_data = dict(explicit_data)
            elif binding.get("user_parameter_name"):
                input_data = linked_input_data(
                    str(binding["user_parameter_name"]),
                    str(binding.get("type_ref_path") or ""),
                )
            elif binding.get("dynamic_input_script_path"):
                input_data = dynamic_input_data(
                    str(binding["dynamic_input_script_path"])
                )
            else:
                input_data = literal_input_data(
                    str(binding["input_struct_ref_path"]),
                    binding.get("value"),
                )
            calls.append(set_stack_input_call(stack_ref, input_data))
            readback_refs.append(stack_ref)
        return _submit_niagara_mutation_scenario(
            interface_name="configure_niagara_particle_controls",
            system_path=system_path,
            calls=calls,
            target_packages=target_packages,
            apply=apply,
            pre_steps=pre_steps,
            readback_refs=readback_refs,
            endpoint=endpoint,
            project=project,
            token=token,
        )
    except (KeyError, TypeError, ValueError) as exc:
        return _niagara_failure(exc)


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
    fields: Optional[List[str]] = None,
    omit: Optional[List[str]] = None,
    max_items: Optional[int] = None,
    cursor: Optional[str] = None,
    artifact_threshold_bytes: int = DEFAULT_RESULT_ARTIFACT_THRESHOLD,
) -> Dict[str, Any]:
    """Call a grouped operation with optional projection, pagination, and Artifact spill."""
    call_kwargs = dict(kwargs or {})
    embedded_result_options = call_kwargs.pop("_result_options", None)
    if embedded_result_options is not None:
        if not isinstance(embedded_result_options, dict):
            return {
                "success": False,
                "error": "_result_options must be an object",
                "phase": "schema",
                "retryable": False,
            }
        fields = embedded_result_options.get(
            "_fields", embedded_result_options.get("fields", fields)
        )
        omit = embedded_result_options.get(
            "_omit", embedded_result_options.get("omit", omit)
        )
        max_items = embedded_result_options.get("max_items", max_items)
        cursor = embedded_result_options.get("cursor", cursor)
        artifact_threshold_bytes = embedded_result_options.get(
            "artifact_threshold_bytes", artifact_threshold_bytes
        )
    wrapper_class, entry = _resolve_manifest_function(library, function)
    if wrapper_class == "Perf" and function in {
        "parse_trace_to_summary", "parse_alloc_trace_to_summary",
        "parse_net_trace_to_summary", "parse_cook_trace_to_summary",
    }:
        return {
            "success": False, "phase": "schema", "retryable": False,
            "error_code": "synchronous_analysis_disabled",
            "error": "Use Perf.start_trace_analysis, then poll get_trace_analysis_status/get_trace_analysis_result; cancel with cancel_trace_analysis.",
        }
    if entry is None:
        return {
            "success": False,
            "error": f"unknown UnrealBridge operation {library}.{function}",
            "phase": "schema",
            "retryable": False,
        }
    validation_error = _validate_call_kwargs(entry, call_kwargs)
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
    raw_result = _execute_code(
        _call_code(wrapper_class, function, call_kwargs, param_schemas),
        endpoint=endpoint,
        project=project,
        token=token,
        timeout=timeout,
        no_preflight=no_preflight,
    )
    needs_shape = any(
        option is not None for option in (fields, omit, max_items, cursor)
    ) or len(json.dumps(raw_result, ensure_ascii=False, default=str).encode("utf-8")) > artifact_threshold_bytes
    if not needs_shape:
        return raw_result
    try:
        shaped = _shape_response(
            raw_result,
            fields=fields,
            omit=omit,
            max_items=max_items,
            cursor=cursor,
            artifact_threshold_bytes=artifact_threshold_bytes,
            query_hash=_query_hash(
                {"kind": "call", "library": wrapper_class, "function": function, "kwargs": call_kwargs}
            ),
            endpoint=endpoint,
            project=project,
            token=token,
        )
        shaped["call_success"] = raw_result.get("success") if isinstance(raw_result, dict) else None
        return shaped
    except ValueError as exc:
        return {"success": False, "phase": "pagination", "retryable": False, "error": str(exc)}


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
def bridge_submit_upgrade_validation(
    request: Dict[str, Any],
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
) -> Dict[str, Any]:
    """Validate new authoring scope against fresh native state. Never edits or saves.

    Poll the returned durable Job. Validation is not proof that an authoring
    executor is implemented or that a later mutation will pass revision checks.
    """
    try:
        normalized = upgrade_contract.normalize_request(request)
    except upgrade_contract.UpgradeFault as exc:
        return exc.result()
    payload = upgrade_contract.canonical_bytes(normalized).decode("utf-8")
    code = (
        "import json\nfrom unreal_bridge import Upgrade\n"
        f"_ub_upgrade_result = Upgrade.validate_upgrade_request(request_json={payload!r})\n"
        "print(_ub_upgrade_result)\n"
    )
    result = bridge_submit_job(
        code, endpoint=endpoint, project=project, token=token,
        timeout=float(normalized["timeout_seconds"]),
        run_timeout=float(normalized["timeout_seconds"]),
        idempotency_key="upgrade-validation:" + normalized["request_id"],
    )
    result = dict(result)
    result.update(request_id=normalized["request_id"], save_policy="never",
                  input_digest="sha1:" + hashlib.sha1(payload.encode("utf-8")).hexdigest())
    if not result.get("success"):
        conflict = "different script" in str(result.get("error", ""))
        result.update(error_code="ValidationFailed" if conflict else "NeedsReconciliation",
                      status="rejected" if conflict else "needs_reconciliation",
                      retryable=False, side_effect_state="none" if conflict else "unknown")
    return result


@mcp.tool()
def bridge_submit_sr_scenario(
    request: Dict[str, Any],
    endpoint: Optional[str] = None,
    project: Optional[str] = None,
    token: Optional[str] = None,
) -> Dict[str, Any]:
    """Submit a typed ShooterRoyal scenario operation; native server ownership is checked again.

    Interactive runs remain for user feedback. Never infer cleanup from Job
    completion; use the explicit release operation and inspect cleanup_state.
    """
    from project_adapters import shooterroyal_scenarios
    try:
        normalized = shooterroyal_scenarios.normalize(request)
        code = shooterroyal_scenarios.build_script(normalized)
    except (ValueError, TypeError, OverflowError) as exc:
        return {"success": False, "error_code": "ValidationFailed", "error": str(exc),
                "retryable": False, "side_effect_state": "none"}
    return bridge_submit_job(code, idempotency_key="sr-scenario:" + normalized["request_id"],
        world_handle=normalized["world_handle"], run_timeout=60,
        endpoint=endpoint, project=project, token=token)


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
