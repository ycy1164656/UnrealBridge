#!/usr/bin/env python3
"""Live reliability soak for the embedded UnrealBridge HTTP endpoint."""

from __future__ import annotations

import argparse
import http.client
import json
import statistics
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
from pathlib import Path


def read_token(explicit: str, token_file: Path) -> str:
    if explicit:
        return explicit
    if token_file.exists():
        return token_file.read_text(encoding="utf-8").strip()
    raise RuntimeError(f"HTTP token not found: {token_file}")


class Client:
    def __init__(self, endpoint: str, token: str, timeout: float) -> None:
        self.endpoint = endpoint.rstrip("/")
        self.token = token
        self.timeout = timeout

    def request(self, path: str, *, method: str = "GET", body: dict | None = None) -> tuple[int, dict | None]:
        headers = {
            "Authorization": f"Bearer {self.token}",
            "MCP-Protocol-Version": "2025-11-25",
        }
        data = None
        if body is not None:
            headers["Content-Type"] = "application/json"
            data = json.dumps(body).encode("utf-8")
        request = urllib.request.Request(self.endpoint + path, data=data, headers=headers, method=method)
        try:
            with urllib.request.urlopen(request, timeout=self.timeout) as response:
                raw = response.read()
                return response.status, json.loads(raw) if raw else None
        except urllib.error.HTTPError as error:
            with error:
                raw = error.read()
                return error.code, json.loads(raw) if raw else None

    def submit(
        self,
        script: str,
        idempotency_key: str,
        *,
        poll_script: str = "",
        poll_interval_seconds: float = 0.1,
        run_timeout_seconds: float = 60.0,
    ) -> dict:
        payload = {
            "script": script,
            "idempotency_key": idempotency_key,
            "queue_timeout_seconds": 30,
        }
        if poll_script:
            payload.update(
                {
                    "poll_script": poll_script,
                    "poll_interval_seconds": poll_interval_seconds,
                    "run_timeout_seconds": run_timeout_seconds,
                }
            )
        status, body = self.request(
            "/unrealbridge/jobs",
            method="POST",
            body=payload,
        )
        if status != 202 or body is None:
            raise RuntimeError(f"Job submit failed: HTTP {status}: {body}")
        return body

    def disconnect_after_submit(self, script: str, idempotency_key: str) -> None:
        parsed = urllib.parse.urlsplit(self.endpoint)
        connection = http.client.HTTPConnection(parsed.hostname, parsed.port, timeout=self.timeout)
        payload = json.dumps(
            {
                "script": script,
                "idempotency_key": idempotency_key,
                "queue_timeout_seconds": 30,
            }
        ).encode("utf-8")
        connection.request(
            "POST",
            "/unrealbridge/jobs",
            body=payload,
            headers={
                "Authorization": f"Bearer {self.token}",
                "Content-Type": "application/json",
                "MCP-Protocol-Version": "2025-11-25",
            },
        )
        connection.close()

    def wait_terminal(self, job_id: str, deadline: float) -> dict:
        while time.monotonic() < deadline:
            status, body = self.request(f"/unrealbridge/jobs/{job_id}")
            if status != 200 or body is None:
                raise RuntimeError(f"Job poll failed: HTTP {status}: {body}")
            if body.get("terminal"):
                return body
            time.sleep(0.05)
        raise TimeoutError(f"Job did not finish before deadline: {job_id}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--endpoint", default="http://127.0.0.1:11438")
    parser.add_argument("--token", default="")
    parser.add_argument(
        "--token-file",
        type=Path,
        default=Path(r"C:\dev\ShooterRoyal\Saved\UnrealBridge\http-token.txt"),
    )
    parser.add_argument("--health-probes", type=int, default=100)
    parser.add_argument("--job-seconds", type=float, default=60.0)
    parser.add_argument("--request-timeout", type=float, default=5.0)
    parser.add_argument("--deadline-seconds", type=float, default=120.0)
    parser.add_argument("--max-health-latency-ms", type=float, default=1000.0)
    parser.add_argument("--json-output", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    started = time.monotonic()
    try:
        token = read_token(args.token, args.token_file)
        client = Client(args.endpoint, token, args.request_timeout)
        key = f"soak-long-{uuid.uuid4()}"
        poll_deadline = time.time() + max(0.1, args.job_seconds)
        long_script = "print('unrealbridge polling soak started')"
        poll_script = (
            "import json, time\n"
            f"print(json.dumps({{'complete': time.time() >= {poll_deadline!r}, "
            "'success': True, 'output': 'unrealbridge polling soak'}))"
        )
        submit_options = {
            "poll_script": poll_script,
            "poll_interval_seconds": 0.05,
            "run_timeout_seconds": max(args.deadline_seconds, args.job_seconds + 5.0),
        }
        submitted = client.submit(long_script, key, **submit_options)
        duplicate = client.submit(long_script, key, **submit_options)
        if submitted["job_id"] != duplicate["job_id"] or not duplicate.get("deduplicated"):
            raise AssertionError("REST idempotency did not return the same Job")

        running_deadline = time.monotonic() + min(5.0, args.deadline_seconds)
        while time.monotonic() < running_deadline:
            status, snapshot = client.request(f"/unrealbridge/jobs/{submitted['job_id']}")
            if status == 200 and snapshot and snapshot.get("job_state") == "running":
                break
            time.sleep(0.02)
        else:
            raise AssertionError("Polling Job did not enter running state")

        health_latencies: list[float] = []
        health_failures: list[str] = []
        running_job_mismatches = 0
        for _ in range(args.health_probes):
            probe_start = time.perf_counter()
            status, body = client.request("/unrealbridge/health")
            latency_ms = (time.perf_counter() - probe_start) * 1000.0
            health_latencies.append(latency_ms)
            if status != 200 or body is None:
                health_failures.append(f"HTTP {status}: {body}")
            elif body.get("running_job_id") != submitted["job_id"]:
                job_status, snapshot = client.request(
                    f"/unrealbridge/jobs/{submitted['job_id']}"
                )
                if job_status == 200 and snapshot and snapshot.get("terminal"):
                    health_failures.append(
                        f"Polling Job ended before all health probes: "
                        f"{len(health_latencies)}/{args.health_probes}"
                    )
                    break
                running_job_mismatches += 1
            time.sleep(0.01)

        terminal = client.wait_terminal(
            submitted["job_id"], time.monotonic() + args.deadline_seconds
        )
        if terminal.get("job_state") != "succeeded":
            raise AssertionError(f"Long Job failed: {terminal}")

        reconnect_key = f"soak-reconnect-{uuid.uuid4()}"
        reconnect_script = "print('disconnect recovery')"
        client.disconnect_after_submit(reconnect_script, reconnect_key)
        time.sleep(0.05)
        recovered = client.submit(reconnect_script, reconnect_key)
        recovered_terminal = client.wait_terminal(
            recovered["job_id"], time.monotonic() + args.deadline_seconds
        )
        if recovered_terminal.get("job_state") != "succeeded":
            raise AssertionError(f"Disconnected Job was not recoverable: {recovered_terminal}")

        max_latency = max(health_latencies, default=0.0)
        result = {
            "passed": (
                not health_failures
                and running_job_mismatches == 0
                and max_latency <= args.max_health_latency_ms
            ),
            "endpoint": args.endpoint,
            "health_probe_count": len(health_latencies),
            "health_failures": health_failures,
            "running_job_mismatches": running_job_mismatches,
            "health_latency_ms": {
                "min": min(health_latencies, default=0.0),
                "mean": statistics.fmean(health_latencies) if health_latencies else 0.0,
                "p95": sorted(health_latencies)[max(0, int(len(health_latencies) * 0.95) - 1)] if health_latencies else 0.0,
                "max": max_latency,
            },
            "long_job_id": submitted["job_id"],
            "recovered_job_id": recovered["job_id"],
            "elapsed_seconds": time.monotonic() - started,
        }
    except Exception as error:  # The JSON report is the soak contract.
        result = {
            "passed": False,
            "error": type(error).__name__,
            "message": str(error),
            "elapsed_seconds": time.monotonic() - started,
        }

    payload = json.dumps(result, ensure_ascii=False, indent=2)
    print(payload)
    if args.json_output:
        args.json_output.parent.mkdir(parents=True, exist_ok=True)
        args.json_output.write_text(payload + "\n", encoding="utf-8")
    return 0 if result.get("passed") else 1


if __name__ == "__main__":
    sys.exit(main())
