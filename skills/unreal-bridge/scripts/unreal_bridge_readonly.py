"""Fail-closed completion of official read-only Jobs shared by audit callers."""
from __future__ import annotations

import hashlib
import json
import time
from typing import Any, Callable

from unreal_bridge_workflows import last_json_object


def complete_readonly(submitted: dict, *, wait: Callable, read_artifact: Callable,
                      timeout: float, toolset: str, tool: str) -> dict:
    """A caller deadline never cancels a durable Job or turns it into empty data."""
    metadata = {key: submitted[key] for key in
                ("job_id", "provider", "schema_hash", "manifest_hash") if key in submitted}
    metadata.update(toolset=toolset, tool=tool, side_effect_state="none")
    deadline = time.monotonic() + max(0.0, timeout)
    snapshot = submitted

    def failure(code: str, detail: Any) -> dict:
        return {**metadata, "success": False, "status": "inconclusive",
                "result_complete": False, "error_code": code, "error": str(detail),
                "job_state": snapshot.get("job_state") if isinstance(snapshot, dict) else None,
                "native_response": snapshot}

    if submitted.get("success") is False or not submitted.get("job_id"):
        return failure("submission_failed", submitted.get("error", "missing durable job id"))
    try:
        while not snapshot.get("terminal"):
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return failure("client_timeout", "Job continues; resume using job_id, do not resubmit")
            snapshot = wait(str(submitted["job_id"]), min(30.0, remaining))
            if not isinstance(snapshot, dict):
                return failure("malformed_job_response", "Expected a Job response object")
            if snapshot.get("job_id", submitted["job_id"]) != submitted["job_id"]:
                return failure("job_identity_mismatch", "Wait returned another Job")
            if snapshot.get("success") is False and not snapshot.get("terminal"):
                return failure("wait_failed", snapshot.get("error"))
        metadata["job_state"] = snapshot.get("job_state")
        if snapshot.get("job_state") != "succeeded":
            return failure("job_failed", snapshot.get("error") or snapshot.get("job_result"))
        if isinstance(snapshot.get("job_result"), dict) and snapshot["job_result"].get("success") is False:
            return failure("native_failed", snapshot["job_result"])
        payload = last_json_object(snapshot)
        if payload is None:
            return failure("malformed_native_result", "Terminal Job did not emit an official JSON result")
        metadata["native_result"] = payload
        if payload.get("success") is False or payload.get("ok") is False:
            return failure("native_failed", payload)
        value = payload.get("result", payload)
        envelope = value if isinstance(value, dict) and ("artifact" in value or "page" in value) else payload
        artifact = envelope.get("artifact")
        if artifact:
            artifact_id = artifact.get("artifact_id") if isinstance(artifact, dict) else str(artifact)
            metadata["artifact"] = artifact
            offset, chunks, total = 0, [], None
            while True:
                if time.monotonic() >= deadline:
                    return failure("artifact_timeout", "Full result is available by artifact id")
                part = read_artifact(artifact_id, offset)
                if not isinstance(part, dict):
                    return failure("artifact_read_failed", "Artifact page must be an object")
                if part.get("success") is False or part.get("encoding") != "utf-8" or part.get("offset") != offset:
                    return failure("artifact_read_failed", part)
                total = part.get("size_bytes") if total is None else total
                if total != part.get("size_bytes") or total > 64 * 1024 * 1024:
                    return failure("artifact_budget_or_revision", "Artifact changed or exceeds 64 MiB")
                chunks.append(part["content"])
                next_offset = part.get("next_offset")
                if next_offset is None:
                    break
                if next_offset <= offset:
                    return failure("artifact_cursor_invalid", part)
                offset = next_offset
            raw = "".join(chunks).encode("utf-8")
            expected = artifact.get("sha256", str(artifact_id).split(".")[0]) if isinstance(artifact, dict) else str(artifact_id).split(".")[0]
            if len(raw) != total or hashlib.sha256(raw).hexdigest() != expected:
                return failure("artifact_hash_mismatch", artifact_id)
            value = json.loads(raw)
        elif (envelope.get("truncated") or envelope.get("result_truncated_to_page")
              or envelope.get("next_cursor") or (envelope.get("page") or {}).get("next_cursor")):
            metadata.update(cursor=envelope.get("next_cursor") or (envelope.get("page") or {}).get("next_cursor"), truncated=True)
            return failure("incomplete_result", "Full artifact or complete pagination required")
        if isinstance(value, dict) and "returnValue" in value:
            value = value["returnValue"]
        return {**metadata, "success": True, "result_complete": True, "result": value}
    except TimeoutError as exc:
        return failure("client_timeout", exc)
    except (ValueError, TypeError, KeyError, AttributeError, OSError) as exc:
        return failure("invalid_result", exc)
