#!/usr/bin/env python3
"""Compact discovery, result shaping, artifacts, and durable scenarios.

This module intentionally contains no MCP or Unreal imports.  The stdio MCP
adapter supplies transport callbacks, which keeps the policy/orchestration
layer unit-testable without a running editor.
"""

from __future__ import annotations

import base64
import copy
import hashlib
import json
import os
import re
import threading
import time
import traceback
import uuid
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Dict, Iterable, List, Optional, Sequence, Tuple


DEFAULT_PAGE_SIZE = 50
MAX_PAGE_SIZE = 500
DEFAULT_ARTIFACT_THRESHOLD = 256 * 1024


def _json_bytes(value: Any) -> bytes:
    return json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), default=str
    ).encode("utf-8")


def last_json_object(response: Dict[str, Any]) -> Optional[Dict[str, Any]]:
    """Return the last JSON object emitted by a bridge/job response.

    Terminal durable-job responses keep the actual script output in
    ``job_result.output`` while the envelope's top-level ``output`` remains the
    job id for compatibility.  Prefer the nested payload, then fall back to the
    legacy synchronous location.
    """
    candidates: List[Any] = []
    job_result = response.get("job_result")
    if isinstance(job_result, dict):
        candidates.append(job_result.get("output"))
    candidates.append(response.get("output"))
    for candidate in candidates:
        # Native JSON serializers may emit a single pretty-printed object. Parse
        # the whole payload first so a nested one-line object cannot be mistaken
        # for the response envelope.
        try:
            value=json.loads(str(candidate or ""))
            if isinstance(value,dict):
                return value
        except json.JSONDecodeError:
            pass
        for line in reversed(str(candidate or "").splitlines()):
            try:
                value = json.loads(line)
            except json.JSONDecodeError:
                continue
            if isinstance(value, dict):
                return value
    return None


def _stable_hash(value: Any) -> str:
    return hashlib.sha256(_json_bytes(value)).hexdigest()


def _snake(value: str) -> str:
    value = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", value)
    value = re.sub(r"[^a-zA-Z0-9]+", "_", value)
    return value.strip("_").lower()


def _encode_cursor(payload: Dict[str, Any]) -> str:
    raw = _json_bytes(payload)
    return base64.urlsafe_b64encode(raw).decode("ascii").rstrip("=")


def _decode_cursor(cursor: Optional[str]) -> Dict[str, Any]:
    if not cursor:
        return {}
    try:
        padded = cursor + "=" * (-len(cursor) % 4)
        decoded = json.loads(base64.urlsafe_b64decode(padded).decode("utf-8"))
    except (ValueError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError("invalid cursor") from exc
    if not isinstance(decoded, dict):
        raise ValueError("invalid cursor payload")
    return decoded


def _get_path(value: Any, dotted_path: str) -> Tuple[bool, Any]:
    current = value
    for segment in dotted_path.split("."):
        if isinstance(current, dict) and segment in current:
            current = current[segment]
        elif isinstance(current, (list, tuple)) and segment.isdigit():
            index = int(segment)
            if index >= len(current):
                return False, None
            current = current[index]
        else:
            return False, None
    return True, current


def _set_path(target: Dict[str, Any], dotted_path: str, value: Any) -> None:
    parts = dotted_path.split(".")
    current = target
    for segment in parts[:-1]:
        child = current.get(segment)
        if not isinstance(child, dict):
            child = {}
            current[segment] = child
        current = child
    current[parts[-1]] = value


def _delete_path(value: Any, dotted_path: str) -> None:
    parts = dotted_path.split(".")
    current = value
    for segment in parts[:-1]:
        if not isinstance(current, dict) or segment not in current:
            return
        current = current[segment]
    if isinstance(current, dict):
        current.pop(parts[-1], None)


def project_fields(
    value: Any,
    fields: Optional[Sequence[str]] = None,
    omit: Optional[Sequence[str]] = None,
) -> Any:
    """Project/omit dotted JSON paths without mutating the caller's object."""
    if isinstance(value, (list, tuple)):
        return [project_fields(item, fields, omit) for item in value]
    if fields:
        projected: Dict[str, Any] = {}
        for path in fields:
            found, selected = _get_path(value, path)
            if found:
                _set_path(projected, path, copy.deepcopy(selected))
        result: Any = projected
    else:
        result = copy.deepcopy(value)
    for path in omit or ():
        _delete_path(result, path)
    return result


def _primary_list(value: Any) -> Tuple[Optional[str], Optional[List[Any]]]:
    if isinstance(value, list):
        return "$", value
    if not isinstance(value, dict):
        return None, None
    if isinstance(value.get("items"), list):
        return "items", value["items"]
    for key in sorted(value):
        if isinstance(value[key], list):
            return key, value[key]
    return None, None


def paginate_value(
    value: Any,
    *,
    max_items: Optional[int] = None,
    cursor: Optional[str] = None,
    query_hash: Optional[str] = None,
) -> Dict[str, Any]:
    """Return a stable pagination envelope around the primary result list."""
    size = DEFAULT_PAGE_SIZE if max_items is None else int(max_items)
    size = max(1, min(size, MAX_PAGE_SIZE))
    path, items = _primary_list(value)
    if items is None:
        return {"value": value, "page": None}

    identity = query_hash or _stable_hash({"path": path, "items": items})
    state = _decode_cursor(cursor)
    if state and (state.get("hash") != identity or state.get("path") != path):
        raise ValueError("cursor does not belong to this result/query")
    offset = int(state.get("offset", 0))
    if offset < 0 or offset > len(items):
        raise ValueError("cursor offset is out of range")
    page_items = items[offset : offset + size]
    next_offset = offset + len(page_items)
    next_cursor = None
    if next_offset < len(items):
        next_cursor = _encode_cursor(
            {"v": 1, "hash": identity, "path": path, "offset": next_offset}
        )

    if path == "$":
        paged_value: Any = page_items
    else:
        paged_value = copy.deepcopy(value)
        paged_value[path] = page_items
    return {
        "value": paged_value,
        "page": {
            "path": path,
            "offset": offset,
            "returned": len(page_items),
            "total": len(items),
            "next_cursor": next_cursor,
        },
    }


@dataclass(frozen=True)
class ArtifactRecord:
    artifact_id: str
    path: str
    sha256: str
    size_bytes: int
    media_type: str
    kind: str

    def as_dict(self) -> Dict[str, Any]:
        return {
            "artifact_id": self.artifact_id,
            "path": self.path,
            "sha256": self.sha256,
            "size_bytes": self.size_bytes,
            "media_type": self.media_type,
            "kind": self.kind,
        }


class ArtifactStore:
    """Atomic, content-addressed JSON/text/binary artifact storage."""

    def __init__(self, root: os.PathLike[str] | str):
        # Resolve only after creating the directory.  On Windows Store/App
        # container paths a parent can be a reparse point; resolving a missing
        # child before mkdir and its files after mkdir can otherwise produce
        # two different canonical prefixes and a false "escaped store" error.
        unresolved_root = Path(root).expanduser()
        unresolved_root.mkdir(parents=True, exist_ok=True)
        self.root = unresolved_root.resolve()

    def _safe_path(self, artifact_id: str) -> Path:
        if not re.fullmatch(r"[a-f0-9]{64}(?:\.[a-z0-9]+)?", artifact_id):
            raise ValueError("invalid artifact id")
        path = (self.root / artifact_id).resolve()
        try:
            path.relative_to(self.root)
        except ValueError:
            raise ValueError("artifact path escaped store")
        return path

    def put(
        self,
        value: Any,
        *,
        kind: str = "json",
        media_type: str = "application/json",
    ) -> ArtifactRecord:
        if isinstance(value, bytes):
            data = value
            suffix = ".bin"
        elif isinstance(value, str) and media_type != "application/json":
            data = value.encode("utf-8")
            suffix = ".txt"
        else:
            data = json.dumps(value, ensure_ascii=False, indent=2, default=str).encode("utf-8")
            suffix = ".json"
        digest = hashlib.sha256(data).hexdigest()
        artifact_id = digest + suffix
        path = self._safe_path(artifact_id)
        if not path.exists():
            temp = path.with_suffix(path.suffix + f".{uuid.uuid4().hex}.tmp")
            temp.write_bytes(data)
            os.replace(temp, path)
        return ArtifactRecord(
            artifact_id=artifact_id,
            path=str(path),
            sha256=digest,
            size_bytes=len(data),
            media_type=media_type,
            kind=kind,
        )

    def read(
        self, artifact_id: str, *, offset: int = 0, max_bytes: int = 64 * 1024
    ) -> Dict[str, Any]:
        path = self._safe_path(artifact_id)
        if not path.is_file():
            raise FileNotFoundError(artifact_id)
        size = path.stat().st_size
        if offset < 0 or offset > size:
            raise ValueError("artifact offset is out of range")
        max_bytes = max(1, min(int(max_bytes), 1024 * 1024))
        with path.open("rb") as stream:
            stream.seek(offset)
            chunk = stream.read(max_bytes)
        next_offset = offset + len(chunk)
        return {
            "artifact_id": artifact_id,
            "offset": offset,
            "size_bytes": size,
            "next_offset": next_offset if next_offset < size else None,
            "encoding": "utf-8" if path.suffix in {".json", ".txt"} else "base64",
            "content": (
                chunk.decode("utf-8")
                if path.suffix in {".json", ".txt"}
                else base64.b64encode(chunk).decode("ascii")
            ),
        }


def shape_result(
    value: Any,
    *,
    fields: Optional[Sequence[str]] = None,
    omit: Optional[Sequence[str]] = None,
    max_items: Optional[int] = None,
    cursor: Optional[str] = None,
    artifact_store: Optional[ArtifactStore] = None,
    artifact_threshold_bytes: int = DEFAULT_ARTIFACT_THRESHOLD,
    query_hash: Optional[str] = None,
) -> Dict[str, Any]:
    projected = project_fields(value, fields, omit)
    page = paginate_value(
        projected, max_items=max_items, cursor=cursor, query_hash=query_hash
    )
    full_size = len(_json_bytes(projected))
    result = {
        "result": page["value"],
        "page": page["page"],
        "full_size_bytes": full_size,
    }
    if artifact_store and full_size > max(1024, int(artifact_threshold_bytes)):
        record = artifact_store.put(projected, kind="large-result")
        result["artifact"] = record.as_dict()
        result["result_truncated_to_page"] = True
        inline_size = len(_json_bytes(page["value"]))
        if page["page"] is None or inline_size > max(
            1024, int(artifact_threshold_bytes)
        ):
            if isinstance(projected, dict):
                summary: Dict[str, Any] = {
                    "type": "object",
                    "key_count": len(projected),
                    "keys": sorted(str(key) for key in projected)[:50],
                }
            elif isinstance(projected, (list, tuple)):
                summary = {"type": "array", "item_count": len(projected)}
            elif isinstance(projected, str):
                summary = {
                    "type": "string",
                    "character_count": len(projected),
                }
            else:
                summary = {"type": type(projected).__name__}
            result["result"] = {
                "artifact_only": True,
                "summary": summary,
            }
            result["result_truncated_to_artifact"] = True
    return result


def _manifest_records(manifest: Dict[str, Any]) -> Iterable[Dict[str, Any]]:
    for full_library, library_entry in sorted(manifest.get("libraries", {}).items()):
        if not (full_library.startswith("UnrealBridge") and full_library.endswith("Library")):
            continue
        domain = _snake(full_library[len("UnrealBridge") : -len("Library")])
        for function, entry in sorted(library_entry.get("functions", {}).items()):
            description = entry.get("tooltip") or entry.get("description") or ""
            yield {
                "id": f"bridge:{domain}.{function}",
                "provider": entry.get("provider", "UnrealBridge"),
                "domain": domain,
                "library": full_library,
                "toolset": None,
                "function": function,
                "description": description,
                "risk": entry.get("risk", "Unknown"),
                "execution": entry.get("execution", "GameThreadShort"),
                "save_behavior": entry.get("save_behavior", "Never"),
                "schema": entry,
            }


def _official_records(catalog: Any) -> Iterable[Dict[str, Any]]:
    if isinstance(catalog, dict) and isinstance(catalog.get("toolsets"), list):
        toolsets = catalog["toolsets"]
    elif isinstance(catalog, list):
        toolsets = catalog
    else:
        return
    for toolset in toolsets:
        if not isinstance(toolset, dict):
            continue
        toolset_name = str(toolset.get("name", ""))
        module = str(toolset.get("module", "official"))
        for tool in toolset.get("tools", []):
            if not isinstance(tool, dict):
                continue
            raw_name = str(tool.get("tool") or tool.get("name") or "")
            function = raw_name.rsplit(".", 1)[-1]
            record_toolset = toolset_name
            if raw_name.startswith(toolset_name + "."):
                raw_name = raw_name[len(toolset_name) + 1 :]
            yield {
                "id": f"epic:{record_toolset}.{function}",
                "provider": tool.get("provider", "EpicToolsetRegistry"),
                "domain": _snake(module),
                "library": None,
                "toolset": record_toolset,
                "function": function,
                "description": tool.get("description", ""),
                "risk": tool.get("risk", "Unknown"),
                "execution": tool.get("execution", "PollingJob"),
                "save_behavior": tool.get("save_behavior", "ProviderDefined"),
                "classification": tool.get("classification"),
                "bridge_execution": tool.get("bridge_execution"),
                "schema": tool,
            }


class ToolIndex:
    def __init__(self, manifest: Dict[str, Any], official_catalog: Any = None):
        records = list(_manifest_records(manifest))
        if official_catalog is not None:
            records.extend(_official_records(official_catalog))
        self.records = sorted(records, key=lambda item: item["id"])
        self.by_id = {record["id"]: record for record in self.records}

    def domains(self, provider: Optional[str] = None) -> List[Dict[str, Any]]:
        groups: Dict[Tuple[str, str], Dict[str, Any]] = {}
        for record in self.records:
            if provider and record["provider"].lower() != provider.lower():
                continue
            key = (record["provider"], record["domain"])
            group = groups.setdefault(
                key,
                {
                    "provider": record["provider"],
                    "domain": record["domain"],
                    "tool_count": 0,
                    "read_only": 0,
                    "mutating": 0,
                    "destructive": 0,
                },
            )
            group["tool_count"] += 1
            risk = str(record.get("risk", "")).lower()
            if "destructive" in risk:
                group["destructive"] += 1
            elif "read" in risk:
                group["read_only"] += 1
            else:
                group["mutating"] += 1
        return sorted(groups.values(), key=lambda item: (item["provider"], item["domain"]))

    def search(
        self,
        query: str = "",
        *,
        domain: Optional[str] = None,
        provider: Optional[str] = None,
        risk: Optional[str] = None,
    ) -> List[Dict[str, Any]]:
        terms = [term for term in re.split(r"\s+", query.lower().strip()) if term]
        matches: List[Tuple[int, Dict[str, Any]]] = []
        for record in self.records:
            if domain and record["domain"].lower() != domain.lower():
                continue
            if provider and record["provider"].lower() != provider.lower():
                continue
            if risk and str(record.get("risk", "")).lower() != risk.lower():
                continue
            haystack = " ".join(
                str(record.get(key, ""))
                for key in ("id", "domain", "function", "description", "classification")
            ).lower()
            if terms and not all(term in haystack for term in terms):
                continue
            score = 0
            for term in terms:
                if term == record["function"].lower():
                    score += 100
                elif term in record["function"].lower():
                    score += 30
                elif term in record["id"].lower():
                    score += 15
                else:
                    score += 1
            matches.append((score, record))
        matches.sort(key=lambda item: (-item[0], item[1]["id"]))
        return [copy.deepcopy(item[1]) for item in matches]

    def describe(self, tool_ids: Sequence[str]) -> List[Dict[str, Any]]:
        return [copy.deepcopy(self.by_id[tool_id]) for tool_id in tool_ids if tool_id in self.by_id]


def _assertion_value(output: Any, assertion: Dict[str, Any]) -> Tuple[bool, str]:
    path = str(assertion.get("path", ""))
    found, actual = _get_path(output, path) if path else (True, output)
    op = str(assertion.get("op", "eq"))
    expected = assertion.get("value")
    if op == "exists":
        passed = found
    elif op == "not_exists":
        passed = not found
    elif not found:
        passed = False
    elif op == "eq":
        passed = actual == expected
    elif op == "ne":
        passed = actual != expected
    elif op == "contains":
        passed = expected in actual
    elif op == "matches":
        passed = re.search(str(expected), str(actual)) is not None
    elif op == "lt":
        passed = actual < expected
    elif op == "lte":
        passed = actual <= expected
    elif op == "gt":
        passed = actual > expected
    elif op == "gte":
        passed = actual >= expected
    else:
        return False, f"unknown assertion op {op!r}"
    return passed, f"{path or '$'} {op} {expected!r}; actual={actual!r}"


class ScenarioManager:
    """Run scenarios in background threads and persist state after every step."""

    def __init__(
        self,
        root: os.PathLike[str] | str,
        execute_step: Callable[[Dict[str, Any], float], Dict[str, Any]],
        rollback_step: Optional[Callable[[Dict[str, Any], Dict[str, Any]], Dict[str, Any]]] = None,
        cleanup: Optional[Callable[[Dict[str, Any]], Dict[str, Any]]] = None,
    ):
        self.root = Path(root).resolve()
        self.root.mkdir(parents=True, exist_ok=True)
        self.execute_step = execute_step
        self.rollback_step = rollback_step
        self.cleanup = cleanup
        self._lock = threading.RLock()
        self._cancelled: set[str] = set()
        self._threads: Dict[str, threading.Thread] = {}

    def _path(self, scenario_id: str) -> Path:
        if not re.fullmatch(r"scenario-[a-f0-9]{32}", scenario_id):
            raise ValueError("invalid scenario id")
        path = (self.root / f"{scenario_id}.json").resolve()
        if self.root not in path.parents:
            raise ValueError("scenario path escaped store")
        return path

    def _write(self, state: Dict[str, Any]) -> None:
        path = self._path(state["scenario_id"])
        payload = json.dumps(state, ensure_ascii=False, indent=2, default=str)
        with self._lock:
            temp = path.with_suffix(f".json.{uuid.uuid4().hex}.tmp")
            temp.write_text(payload, encoding="utf-8")
            try:
                # Windows denies ReplaceFile while another thread, an indexer,
                # or antivirus still has the destination open.  In-process
                # readers share this lock; the bounded retry handles the small
                # remaining external-handle window without weakening atomicity.
                for attempt in range(10):
                    try:
                        os.replace(temp, path)
                        return
                    except PermissionError:
                        if attempt == 9:
                            raise
                        time.sleep(min(0.01 * (attempt + 1), 0.1))
            finally:
                if temp.exists():
                    try:
                        temp.unlink()
                    except OSError:
                        pass

    def get(self, scenario_id: str) -> Dict[str, Any]:
        path = self._path(scenario_id)
        with self._lock:
            if not path.is_file():
                raise FileNotFoundError(scenario_id)
            return json.loads(path.read_text(encoding="utf-8"))

    def submit(self, spec: Dict[str, Any]) -> Dict[str, Any]:
        steps = spec.get("steps")
        if not isinstance(steps, list) or not steps:
            raise ValueError("scenario requires a non-empty steps array")
        ids = [str(step.get("id", "")) for step in steps]
        if any(not item for item in ids) or len(set(ids)) != len(ids):
            raise ValueError("every scenario step requires a unique non-empty id")
        scenario_id = "scenario-" + uuid.uuid4().hex
        now = time.time()
        state = {
            "schema_version": 1,
            "scenario_id": scenario_id,
            "name": spec.get("name") or scenario_id,
            "status": "queued",
            "created_at": now,
            "updated_at": now,
            "current_step": None,
            "spec": copy.deepcopy(spec),
            "steps": [
                {
                    "id": step["id"],
                    "status": "pending",
                    "attempts": 0,
                    "started_at": None,
                    "completed_at": None,
                    "output": None,
                    "error": None,
                    "assertions": [],
                    "rollback": None,
                }
                for step in steps
            ],
            "error": None,
        }
        self._write(state)
        thread = threading.Thread(
            target=self._run, args=(scenario_id,), name=scenario_id, daemon=True
        )
        with self._lock:
            self._threads[scenario_id] = thread
        thread.start()
        return state

    def cancel(self, scenario_id: str) -> Dict[str, Any]:
        with self._lock:
            self._cancelled.add(scenario_id)
            state = self.get(scenario_id)
            state["cancel_requested"] = True
            state["updated_at"] = time.time()
            self._write(state)
        return state

    def _is_cancelled(self, scenario_id: str) -> bool:
        with self._lock:
            return scenario_id in self._cancelled

    def _run(self, scenario_id: str) -> None:
        state = self.get(scenario_id)
        spec = state["spec"]
        state["status"] = "running"
        state["updated_at"] = time.time()
        self._write(state)
        completed_mutations: List[Tuple[Dict[str, Any], Dict[str, Any], int]] = []
        try:
            for index, step in enumerate(spec["steps"]):
                if self._is_cancelled(scenario_id):
                    raise RuntimeError("scenario cancellation requested")
                item = state["steps"][index]
                state["current_step"] = step["id"]
                item["status"] = "running"
                item["started_at"] = time.time()
                state["updated_at"] = time.time()
                self._write(state)

                step_type = str(step.get("type", "call"))
                forced_risks = {
                    "official": "ReadOnly",
                    "official_runtime": "RuntimeInteraction",
                    "official_transactional": "Mutating",
                    "official_transactional_batch": "Mutating",
                    "niagara_binding_tree": "ReadOnly",
                    "niagara_input_guard": "ReadOnly",
                    "niagara_wait_compile": "ReadOnly",
                    "slate_capture": "RuntimeInteraction",
                }
                risk = forced_risks.get(step_type, str(step.get("risk", "Unknown")))
                retry_count = max(0, min(int(step.get("retry", 0)), 5))
                if retry_count and risk.lower() != "readonly" and not step.get("idempotency_key"):
                    raise ValueError(
                        f"step {step['id']} may retry only when ReadOnly or idempotency_key is set"
                    )
                timeout = max(0.1, min(float(step.get("timeout", 60.0)), 3600.0))
                output: Dict[str, Any] = {}
                last_error: Optional[str] = None
                for attempt in range(retry_count + 1):
                    item["attempts"] = attempt + 1
                    try:
                        output = self.execute_step(step, timeout)
                        if not isinstance(output, dict):
                            output = {"result": output}
                        last_error = None
                        break
                    except Exception as exc:  # noqa: BLE001 - persisted structured failure
                        last_error = str(exc)
                        if attempt >= retry_count:
                            raise
                if last_error:
                    raise RuntimeError(last_error)

                # Persist the executed output and register its rollback before
                # evaluating assertions.  A mutating step whose *own* assertion
                # fails has already produced side effects and must participate
                # in the same reverse-order rollback as earlier steps.
                item["output"] = output
                if risk.lower() != "readonly" and output.get("rollback_required", True):
                    completed_mutations.append((step, output, index))
                state["updated_at"] = time.time()
                self._write(state)

                assertions = []
                for assertion in step.get("assertions", []):
                    passed, detail = _assertion_value(output, assertion)
                    assertions.append({"passed": passed, "detail": detail})
                    item["assertions"] = assertions
                    if not passed:
                        raise AssertionError(f"step {step['id']} assertion failed: {detail}")

                item["assertions"] = assertions
                item["status"] = "succeeded"
                item["completed_at"] = time.time()
                state["updated_at"] = time.time()
                self._write(state)

            if self._is_cancelled(scenario_id):
                raise RuntimeError("scenario cancellation requested")
            state["status"] = "succeeded"
            state["current_step"] = None
        except Exception as exc:  # noqa: BLE001 - state must survive arbitrary failures
            state["status"] = "cancelled" if self._is_cancelled(scenario_id) else "failed"
            state["error"] = str(exc)
            if state.get("current_step"):
                for item in state["steps"]:
                    if item["id"] == state["current_step"] and item["status"] == "running":
                        item["status"] = "failed"
                        item["error"] = str(exc)
                        item["completed_at"] = time.time()
                        break
            if self.rollback_step:
                for step, output, index in reversed(completed_mutations):
                    try:
                        rollback = self.rollback_step(step, output)
                    except Exception as rollback_exc:  # noqa: BLE001
                        rollback = {"success": False, "error": str(rollback_exc)}
                    state["steps"][index]["rollback"] = rollback
            state["traceback"] = traceback.format_exc(limit=10)
        finally:
            # Ownership-scoped runtime cleanup is independent of asset rollback
            # and must run on success, failure, timeout and cancellation alike.
            if self.cleanup:
                try:
                    state["cleanup"] = self.cleanup(state)
                except Exception as cleanup_exc:
                    state["cleanup"] = {"success": False, "needs_reconciliation": True, "error": str(cleanup_exc)}
                if not isinstance(state["cleanup"], dict) or not state["cleanup"].get("success"):
                    state["execution_status"] = state["status"]
                    state["status"] = "needs_reconciliation"
                    state["error"] = state["error"] or "Runtime cleanup could not be verified"
            state["updated_at"] = time.time()
            self._write(state)
            with self._lock:
                self._threads.pop(scenario_id, None)
                self._cancelled.discard(scenario_id)
