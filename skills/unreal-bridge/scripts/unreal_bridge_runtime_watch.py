"""Bounded host scheduler for read-only, multi-world PIE snapshots."""

from __future__ import annotations

import copy
from concurrent.futures import Future, ThreadPoolExecutor
import math
import statistics
import threading
import time
import uuid
from typing import Any, Callable


def validate(request: dict[str, Any]) -> dict[str, Any]:
    if not isinstance(request, dict):
        raise ValueError("request must be an object")
    allowed = {"pie_session_id", "world_handles", "actor_name", "class_path", "fields",
               "duration_seconds", "sample_hz", "only_changes"}
    if set(request) - allowed:
        raise ValueError("unknown watch request fields")
    worlds, fields = request.get("world_handles"), request.get("fields")
    if not isinstance(worlds, list) or not 1 <= len(worlds) <= 4 or len(set(worlds)) != len(worlds):
        raise ValueError("1..4 unique exact world_handles required")
    if not all(isinstance(item, str) and item.startswith("ubr:world:") for item in worlds):
        raise ValueError("invalid world handle")
    if not isinstance(fields, list) or not 1 <= len(fields) <= 32:
        raise ValueError("1..32 fields required")
    for item in fields:
        if (not isinstance(item, dict) or set(item) != {"kind", "name"}
                or item["kind"] not in {"property", "gas_attribute", "gameplay_tag"}
                or not isinstance(item["name"], str) or not 1 <= len(item["name"]) <= 128):
            raise ValueError("invalid field selector")
    for key in ("pie_session_id", "actor_name", "class_path"):
        if not isinstance(request.get(key), str) or not 1 <= len(request[key]) <= 512:
            raise ValueError(f"{key} is required and bounded")
    duration = request.get("duration_seconds", 10)
    rate = request.get("sample_hz", 5)
    if type(duration) not in (int, float) or not 0.1 <= duration <= 30:
        raise ValueError("duration_seconds must be 0.1..30")
    if type(rate) not in (int, float) or not 1 <= rate <= 20:
        raise ValueError("sample_hz must be 1..20")
    if type(request.get("only_changes", True)) is not bool:
        raise ValueError("only_changes must be bool")
    return {**request, "duration_seconds": duration, "sample_hz": rate,
            "only_changes": request.get("only_changes", True)}


class RuntimeWatch:
    def __init__(self, request: dict[str, Any], sample: Callable[[dict[str, Any], int], dict[str, Any]],
                 artifact_store: Any):
        self.id = "watch-" + uuid.uuid4().hex
        self.request = validate(request)
        self.sample = sample
        self.artifact_store = artifact_store
        self.status = "queued"
        self.error = ""
        self.cancelled = threading.Event()
        self.lock = threading.RLock()
        self.thread: threading.Thread | None = None
        self.summary: dict[str, Any] = {}

    def start(self) -> None:
        self.thread = threading.Thread(target=self._run, name=self.id, daemon=True)
        self.thread.start()

    def _run(self) -> None:
        started = time.monotonic()
        period = 1.0 / self.request["sample_hz"]
        deadline = started + self.request["duration_seconds"]
        timeline: list[dict[str, Any]] = []
        overhead: list[float] = []
        capture_times: list[float] = []
        dropped = 0
        divergence_at = None
        convergence_at = None
        previous_values = None
        results: dict[int, tuple[float, float, dict[str, Any]]] = {}
        pending: dict[int, Future[tuple[float, float, dict[str, Any]]]] = {}
        with self.lock:
            self.status = "running"
        try:
            def run_sample(sequence: int) -> tuple[float, float, dict[str, Any]]:
                before = time.monotonic()
                try:
                    sample = self.sample(self.request, sequence)
                except Exception as exc:
                    sample = {"ok": False, "error": str(exc)[:512]}
                return before, time.monotonic() - before, sample

            # Jobs spend most of their time in transport and polling; keep at most
            # two read-only requests in flight without building an unbounded queue.
            with ThreadPoolExecutor(max_workers=2, thread_name_prefix=self.id) as executor:
                slots = max(1, math.ceil(self.request["duration_seconds"] * self.request["sample_hz"]))
                for sequence in range(slots):
                    if self.cancelled.is_set():
                        break
                    target = started + sequence * period
                    delay = target - time.monotonic()
                    if delay > 0 and self.cancelled.wait(delay):
                        break
                    for index, future in list(pending.items()):
                        if future.done():
                            results[index] = future.result()
                            del pending[index]
                    if time.monotonic() >= min(deadline, target + period) or len(pending) >= 2:
                        dropped += 1
                        continue
                    pending[sequence] = executor.submit(run_sample, sequence)
                for index, future in pending.items():
                    results[index] = future.result(timeout=10)

            for sequence in sorted(results):
                before, elapsed, sample = results[sequence]
                overhead.append(elapsed)
                if not sample.get("ok"):
                    raise RuntimeError(str(sample.get("error_code") or sample.get("error") or "snapshot_failed"))
                worlds = sample.get("worlds")
                if not isinstance(worlds, list) or len(worlds) != len(self.request["world_handles"]):
                    raise RuntimeError("incomplete_world_snapshot")
                captured = worlds[0].get("host_monotonic_seconds")
                if isinstance(captured, (int, float)):
                    capture_times.append(float(captured))
                values = [world.get("fields") for world in worlds]
                divergent = any(value != values[0] for value in values[1:])
                if divergent and divergence_at is None:
                    divergence_at = sequence
                if divergence_at is not None and not divergent and convergence_at is None:
                    convergence_at = sequence
                entry = {"sequence": sequence, "host_monotonic_seconds": before,
                         "snapshot": sample, "observed_divergence": divergent}
                if previous_values is None or not self.request["only_changes"] or previous_values != values:
                    timeline.append(entry)
                previous_values = copy.deepcopy(values)
            terminal = "cancelled" if self.cancelled.is_set() else "completed"
        except Exception as exc:
            reason = str(exc).lower()
            terminal = ("needs_reconciliation" if any(marker in reason for marker in
                        ("timeout", "stale", "replaced", "snapshot_submit_failed", "connection"))
                        else "failed")
            self.error = str(exc)[:512]
        record = self.artifact_store.put({"schema": "unrealbridge.runtime_watch.v1",
                                          "request": self.request, "timeline": timeline,
                                          "status": terminal, "error": self.error},
                                         kind="runtime_watch_timeline")
        sorted_overhead = sorted(overhead)
        gaps = sorted(max(0.0, later - earlier) for earlier, later in zip(capture_times, capture_times[1:]))
        summary = {"watch_id": self.id, "task_kind": "runtime_watch", "status": terminal,
                   "sample_count": len(overhead), "timeline_events": len(timeline),
                   "dropped_samples": dropped, "observed_divergence_at_sample": divergence_at,
                   "observed_convergence_at_sample": convergence_at,
                   "sample_job_p50_ms": round(statistics.median(overhead) * 1000, 2) if overhead else None,
                   "sample_job_p95_ms": round(sorted_overhead[min(len(overhead)-1, int(len(overhead)*0.95))] * 1000, 2) if overhead else None,
                   "native_capture_count": len(capture_times),
                   "native_capture_gap_p50_ms": round(statistics.median(gaps) * 1000, 2) if gaps else None,
                   "native_capture_gap_p95_ms": round(gaps[min(len(gaps)-1, int(len(gaps)*0.95))] * 1000, 2) if gaps else None,
                   "artifact": record.as_dict(), "error": self.error}
        with self.lock:
            self.summary = summary
            self.status = terminal

    def view(self) -> dict[str, Any]:
        with self.lock:
            return {"success": True, "watch_id": self.id, "task_kind": "runtime_watch",
                    "status": self.status, "cancel_supported": True,
                    "terminal": self.status in {"completed", "cancelled", "failed", "needs_reconciliation"},
                    "side_effect_state": "none", **self.summary}
