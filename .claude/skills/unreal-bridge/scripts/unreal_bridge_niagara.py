#!/usr/bin/env python3
"""Stable high-level argument builders for UE 5.8 Niagara Toolsets."""

from __future__ import annotations

import json
import re
from typing import Any, Dict, Iterable, List, Mapping, Optional, Sequence


SYSTEM_TOOLSET = "NiagaraToolsets.NiagaraToolset_System"
LINKED_INPUT_STRUCT = (
    "/Script/NiagaraEditor.NiagaraExt_StackInputData_Linked"
)
DYNAMIC_INPUT_STRUCT = (
    "/Script/NiagaraEditor.NiagaraExt_StackInputData_DynamicInput"
)


def object_ref(path: str) -> Dict[str, str]:
    path = str(path).strip()
    if not path or not path.startswith("/"):
        raise ValueError(f"expected an Unreal object path beginning with '/'; got {path!r}")
    return {"refPath": path}


def package_from_object_path(path: str) -> str:
    """Normalize /Mount/Asset.Asset or Class'/Mount/Asset.Asset' to a package."""
    value = str(path).strip()
    match = re.match(r"^[A-Za-z0-9_]+['\"](?P<path>/[^'\"]+)['\"]$", value)
    if match:
        value = match.group("path")
    if not value.startswith("/"):
        raise ValueError(f"invalid Unreal object path {path!r}")
    package = value.split(":", 1)[0].split(".", 1)[0].rstrip("/")
    if package.count("/") < 2:
        raise ValueError(f"object path does not identify an asset package: {path!r}")
    return package


def stack_item_reference(
    system_path: str,
    *,
    emitter_name: str = "",
    script_name: str = "",
    module_name: str = "",
    renderer_index: int = -1,
    input_name_stack: Optional[Sequence[str]] = None,
) -> Dict[str, Any]:
    return {
        "system": object_ref(system_path),
        "emitterName": str(emitter_name),
        "scriptName": str(script_name),
        "moduleName": str(module_name),
        "rendererIndex": int(renderer_index),
        "inputNameStack": [str(item) for item in (input_name_stack or [])],
    }


def type_definition(type_ref_path: str) -> Dict[str, Any]:
    return {"classStructOrEnum": object_ref(type_ref_path)}


def normalize_user_parameter(parameter: Mapping[str, Any]) -> Dict[str, Any]:
    """Accept either the official structure or a compact stable structure."""
    if "defaultValue" in parameter and isinstance(parameter.get("type"), Mapping):
        return dict(parameter)
    name = str(parameter.get("name") or "").strip()
    type_path = str(
        parameter.get("type_ref_path")
        or parameter.get("typePath")
        or parameter.get("type")
        or ""
    ).strip()
    if not name or not type_path:
        raise ValueError("each Niagara user parameter requires name and type_ref_path")
    default_struct_path = str(
        parameter.get("default_struct_ref_path")
        or parameter.get("defaultStructPath")
        or type_path
    ).strip()
    default_value: Dict[str, Any] = {"struct": object_ref(default_struct_path)}
    if "default_value" in parameter:
        default_value["value"] = parameter["default_value"]
    elif "defaultValueValue" in parameter:
        default_value["value"] = parameter["defaultValueValue"]
    return {
        "name": name,
        "type": type_definition(type_path),
        "defaultValue": default_value,
        "description": str(parameter.get("description") or ""),
    }


def linked_input_data(
    user_parameter_name: str,
    type_ref_path: str,
) -> Dict[str, Any]:
    name = str(user_parameter_name).strip()
    if not name:
        raise ValueError("user_parameter_name is required")
    if not name.startswith("User."):
        name = f"User.{name}"
    return {
        "struct": object_ref(LINKED_INPUT_STRUCT),
        "value": {
            "linkedVariable": {
                "name": name,
                "type": type_definition(type_ref_path),
            }
        },
    }


def dynamic_input_data(dynamic_input_script_path: str) -> Dict[str, Any]:
    return {
        "struct": object_ref(DYNAMIC_INPUT_STRUCT),
        "value": {"dynamicInputAsset": object_ref(dynamic_input_script_path)},
    }


def literal_input_data(
    struct_ref_path: str,
    value: Optional[Any] = None,
) -> Dict[str, Any]:
    result: Dict[str, Any] = {"struct": object_ref(struct_ref_path)}
    if value is not None:
        result["value"] = value
    return result


def rename_emitter_call(
    system_path: str,
    emitter_name: str,
    new_name: str,
) -> Dict[str, Any]:
    if not str(emitter_name).strip() or not str(new_name).strip():
        raise ValueError("emitter_name and new_name are required")
    return {
        "domain": "niagara",
        "operation": "SetEmitterData",
        "arguments": {
            "emitter": stack_item_reference(
                system_path, emitter_name=emitter_name
            ),
            "emitterData": {
                "propertyValues": json.dumps(
                    {"Name": str(new_name)},
                    ensure_ascii=False,
                    sort_keys=True,
                    separators=(",", ":"),
                )
            },
        },
    }


def set_stack_input_call(
    stack_input_ref: Mapping[str, Any],
    input_data: Mapping[str, Any],
) -> Dict[str, Any]:
    return {
        "domain": "niagara",
        "operation": "SetStackInputData",
        "arguments": {
            "stackInputRef": dict(stack_input_ref),
            "inputData": dict(input_data),
        },
    }


def add_user_variables_call(
    system_path: str,
    parameters: Iterable[Mapping[str, Any]],
) -> Dict[str, Any]:
    normalized = [normalize_user_parameter(item) for item in parameters]
    if not normalized:
        raise ValueError("parameters must contain at least one user parameter")
    return {
        "domain": "niagara",
        "operation": "AddUserVariables",
        "arguments": {
            "system": object_ref(system_path),
            "variablesToAdd": normalized,
        },
    }


def find_instanced_struct_paths(value: Any) -> List[str]:
    paths: List[str] = []
    if isinstance(value, Mapping):
        struct = value.get("struct")
        if isinstance(struct, Mapping) and isinstance(struct.get("refPath"), str):
            paths.append(struct["refPath"])
        for child in value.values():
            paths.extend(find_instanced_struct_paths(child))
    elif isinstance(value, list):
        for child in value:
            paths.extend(find_instanced_struct_paths(child))
    return paths


def existing_binding_kind(value: Any) -> str:
    """Classify GetStackInputData output conservatively for replace guards."""
    paths = find_instanced_struct_paths(value)
    for path in paths:
        marker = "/Script/NiagaraEditor.NiagaraExt_StackInputData_"
        if path.startswith(marker):
            return path[len(marker) :] or "Bound"
    return "Literal" if paths else "Unknown"


def emitter_identity_guard_code(
    system_path: str,
    emitter_name: str,
    emitter_handle_id: str,
) -> str:
    """Return editor Python which fails unless name and stable handle id agree."""
    payload = json.dumps(
        {
            "system_path": system_path,
            "emitter_name": emitter_name,
            "emitter_handle_id": emitter_handle_id,
        },
        ensure_ascii=False,
        sort_keys=True,
    )
    return (
        "import json, unreal\n"
        f"_p=json.loads({payload!r})\n"
        "_s=unreal.UnrealBridgeNiagaraLibrary.get_niagara_system_structure(_p['system_path'])\n"
        "_matches=[e for e in _s.emitters if str(e.name)==_p['emitter_name']]\n"
        "if len(_matches)!=1:\n"
        "    raise RuntimeError('Niagara emitter name is missing or ambiguous: '+_p['emitter_name'])\n"
        "if str(_matches[0].handle_id).lower()!=_p['emitter_handle_id'].lower():\n"
        "    raise RuntimeError('Niagara emitter handle id does not match the named emitter')\n"
        "print(json.dumps({'success':True,'emitter_name':str(_matches[0].name),"
        "'emitter_handle_id':str(_matches[0].handle_id)},ensure_ascii=False))"
    )


__all__ = [
    "DYNAMIC_INPUT_STRUCT",
    "LINKED_INPUT_STRUCT",
    "SYSTEM_TOOLSET",
    "add_user_variables_call",
    "dynamic_input_data",
    "emitter_identity_guard_code",
    "existing_binding_kind",
    "linked_input_data",
    "literal_input_data",
    "normalize_user_parameter",
    "object_ref",
    "package_from_object_path",
    "rename_emitter_call",
    "set_stack_input_call",
    "stack_item_reference",
    "type_definition",
]
