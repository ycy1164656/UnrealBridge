"""Shared, no-write request contract for incremental authoring capabilities.

This module does not dispatch scripts, create transactions or save assets. Native
entry points validate again using current Editor state. JobManager continues to
own request idempotency and recovery; this is not a second job database.
"""
from __future__ import annotations

import copy
import hashlib
import json
import math
import re

SCHEMA = "unrealbridge.upgrade.v1"
MAX_TARGETS = 16
MAX_REQUEST_BYTES = 64 * 1024
ERROR_CODES = frozenset({
    "UnsupportedCapability", "EngineVersionMismatch", "StaleHandle",
    "TargetRevisionMismatch", "ScopeViolation", "DirtyConflict",
    "ValidationFailed", "Timeout", "Cancelled", "NeedsReconciliation",
})
FIELDS = frozenset({
    "schema", "request_id", "operation_id", "project_identity",
    "editor_session_id", "engine_version", "target_packages",
    "expected_revisions", "dry_run", "save_policy", "world_handle",
    "timeout_seconds",
})


class UpgradeFault(ValueError):
    def __init__(self, code: str, message: str):
        self.code = code
        super().__init__(message)

    def result(self):
        return {"ok": False, "status": "rejected", "error_code": self.code,
                "error": str(self), "retryable": False, "side_effect_state": "none"}


def canonical_bytes(value):
    try:
        return json.dumps(value, ensure_ascii=False, sort_keys=True,
                          separators=(",", ":"), allow_nan=False).encode("utf-8")
    except (TypeError, ValueError, RecursionError) as error:
        raise UpgradeFault("ValidationFailed", "Request must be finite JSON") from error


def project_identity(value):
    if not isinstance(value, str) or not value or len(value) > 2048:
        raise UpgradeFault("ScopeViolation", "Explicit .uproject path is required")
    result = value.replace("\\", "/")
    if not re.match(r"^(?:[A-Za-z]:/|/)", result) or not result.lower().endswith(".uproject"):
        raise UpgradeFault("ScopeViolation", "Project identity must be an absolute .uproject path")
    if any(part in (".", "..") for part in result.split("/")) or any(ord(c) < 32 for c in result):
        raise UpgradeFault("ScopeViolation", "Project identity is not canonical")
    return result.casefold() if re.match(r"^[A-Za-z]:/", result) else result


def package_name(value):
    if not isinstance(value, str) or not 3 <= len(value) <= 512:
        raise UpgradeFault("ScopeViolation", "Target must be a canonical package name")
    if not re.fullmatch(r"/(?:[^\s\\.:*?<>\"|/]+/)+[^\s\\.:*?<>\"|/]+", value):
        raise UpgradeFault("ScopeViolation", "Object paths, traversal and wildcards are not package targets")
    if value.split("/")[1].casefold() in {"engine", "script", "temp", "transient", "memory"}:
        raise UpgradeFault("ScopeViolation", "Protected mount cannot be an authoring target")
    if any(ord(c) < 32 for c in value):
        raise UpgradeFault("ScopeViolation", "Control characters are not package targets")
    return value


def normalize_request(request):
    if not isinstance(request, dict) or set(request) - FIELDS:
        raise UpgradeFault("ValidationFailed", "Request contains unknown fields or is not an object")
    if len(canonical_bytes(request)) > MAX_REQUEST_BYTES:
        raise UpgradeFault("ValidationFailed", "Request exceeds 64 KiB")
    value = copy.deepcopy(request)
    if value.get("schema") != SCHEMA:
        raise UpgradeFault("UnsupportedCapability", "Unsupported upgrade request schema")
    if value.get("operation_id") != "upgrade.validate":
        raise UpgradeFault("UnsupportedCapability", "Operation has no implemented upgrade executor")
    for key in ("request_id", "editor_session_id"):
        if not isinstance(value.get(key), str) or not re.fullmatch(r"[A-Za-z0-9_.:-]{1,128}", value[key]):
            raise UpgradeFault("ValidationFailed", f"Invalid {key}")
    value["project_identity"] = project_identity(value.get("project_identity"))
    if not isinstance(value.get("engine_version"), str) or not 1 <= len(value["engine_version"]) <= 128:
        raise UpgradeFault("ValidationFailed", "Explicit engine_version is required")
    targets = value.get("target_packages")
    if not isinstance(targets, list) or len(targets) > MAX_TARGETS:
        raise UpgradeFault("ScopeViolation", "At most 16 explicit package targets are allowed")
    value["target_packages"] = [package_name(target) for target in targets]
    if len({target.casefold() for target in targets}) != len(targets):
        raise UpgradeFault("ScopeViolation", "Duplicate or case-aliased target packages")
    revisions = value.get("expected_revisions")
    if not isinstance(revisions, dict) or set(revisions) != set(targets):
        raise UpgradeFault("TargetRevisionMismatch", "Every target must have exactly one expected revision")
    if any(not isinstance(v, str) or not re.fullmatch(r"(?:[a-f0-9]{40}|absent)", v) for v in revisions.values()):
        raise UpgradeFault("TargetRevisionMismatch", "Expected revision must be a snapshot SHA-1 or absent")
    value.setdefault("dry_run", True)
    if type(value["dry_run"]) is not bool:
        raise UpgradeFault("ValidationFailed", "dry_run must be a Boolean")
    value.setdefault("save_policy", "never")
    if value["save_policy"] != "never":
        raise UpgradeFault("ScopeViolation", "Upgrade operations never implicitly save")
    value.setdefault("timeout_seconds", 30)
    timeout = value["timeout_seconds"]
    if type(timeout) not in (int, float) or not math.isfinite(timeout) or not 1 <= timeout <= 120:
        raise UpgradeFault("ValidationFailed", "timeout_seconds must be finite and between 1 and 120")
    value.setdefault("world_handle", "")
    if not isinstance(value["world_handle"], str) or len(value["world_handle"]) > 256:
        raise UpgradeFault("StaleHandle", "Invalid world handle")
    return value


def validate_request(request, snapshot):
    value = normalize_request(request)
    if not isinstance(snapshot, dict) or snapshot.get("ok") is not True:
        raise UpgradeFault("NeedsReconciliation", "A current native snapshot is required")
    if project_identity(snapshot.get("project_identity")) != value["project_identity"]:
        raise UpgradeFault("ScopeViolation", "Request selects a different project")
    if snapshot.get("editor_session_id") != value["editor_session_id"]:
        raise UpgradeFault("StaleHandle", "Editor session has changed")
    if snapshot.get("engine_version") != value["engine_version"]:
        raise UpgradeFault("EngineVersionMismatch", "Engine version has changed")
    if value["world_handle"] and value["world_handle"] not in snapshot.get("world_handles", []):
        raise UpgradeFault("StaleHandle", "World handle has expired")
    observed = snapshot.get("targets")
    if not isinstance(observed, dict) or set(observed) != set(value["target_packages"]):
        raise UpgradeFault("ScopeViolation", "Snapshot target set does not match the request")
    for target, expected in value["expected_revisions"].items():
        item = observed[target]
        if not isinstance(item, dict) or item.get("revision") != expected:
            raise UpgradeFault("TargetRevisionMismatch", f"Target revision changed: {target}")
        if not value["dry_run"] and item.get("dirty") is not False:
            raise UpgradeFault("DirtyConflict", f"Target is dirty or its state is unknown: {target}")
    return {"ok": True, "status": "validated", "schema": SCHEMA,
            "request_id": value["request_id"], "operation_id": value["operation_id"],
            "request_hash": hashlib.sha256(canonical_bytes(value)).hexdigest(),
            "input_digest": "sha1:" + hashlib.sha1(canonical_bytes(value)).hexdigest(),
            "cleanup_state": "not_required", "applied_operations": [], "dirty_delta": [],
            "error_code": "", "retryable": False, "side_effect_state": "none",
            "save_policy": "never", "request": value}
