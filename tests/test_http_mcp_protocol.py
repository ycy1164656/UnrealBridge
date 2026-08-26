"""Live protocol tests for UnrealBridge's embedded HTTP MCP endpoint."""

from __future__ import annotations

import json
import os
import time
import unittest
import urllib.error
import urllib.request
import uuid
from pathlib import Path


class UnrealBridgeHttpMcpTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.base_url = os.environ.get(
            "UNREAL_BRIDGE_HTTP_ENDPOINT", "http://127.0.0.1:11438"
        ).rstrip("/")
        token = os.environ.get("UNREAL_BRIDGE_HTTP_TOKEN", "")
        if not token:
            token_path = Path(
                os.environ.get(
                    "UNREAL_BRIDGE_HTTP_TOKEN_FILE",
                    r"C:\dev\ShooterRoyal\Saved\UnrealBridge\http-token.txt",
                )
            )
            if token_path.exists():
                token = token_path.read_text(encoding="utf-8").strip()
        if not token:
            raise unittest.SkipTest("HTTP MCP token is not available")
        cls.token = token

    @classmethod
    def request(
        cls,
        path: str,
        *,
        method: str = "GET",
        body: dict | None = None,
        auth: bool = True,
        origin: str | None = None,
        protocol: bool = True,
    ) -> tuple[int, dict | None, dict]:
        headers: dict[str, str] = {}
        if auth:
            headers["Authorization"] = f"Bearer {cls.token}"
        if origin is not None:
            headers["Origin"] = origin
        if body is not None:
            headers["Content-Type"] = "application/json"
            headers["Accept"] = "application/json, text/event-stream"
        if protocol:
            headers["MCP-Protocol-Version"] = "2025-11-25"
        payload = None if body is None else json.dumps(body).encode("utf-8")
        request = urllib.request.Request(
            cls.base_url + path,
            data=payload,
            headers=headers,
            method=method,
        )
        try:
            with urllib.request.urlopen(request, timeout=10) as response:
                raw = response.read()
                return (
                    response.status,
                    json.loads(raw) if raw else None,
                    dict(response.headers),
                )
        except urllib.error.HTTPError as error:
            try:
                raw = error.read()
                return error.code, json.loads(raw) if raw else None, dict(error.headers)
            finally:
                error.close()

    @classmethod
    def mcp(cls, request_id: int, method: str, params: dict | None = None) -> dict:
        message = {"jsonrpc": "2.0", "id": request_id, "method": method}
        if params is not None:
            message["params"] = params
        status, response, _ = cls.request("/mcp", method="POST", body=message)
        if status != 200 or response is None:
            raise AssertionError(f"MCP request failed: HTTP {status}: {response}")
        return response

    def test_auth_origin_and_get_behavior(self) -> None:
        self.assertEqual(self.request("/unrealbridge/health", auth=False)[0], 401)
        self.assertEqual(
            self.request(
                "/unrealbridge/health", origin="https://untrusted.example"
            )[0],
            401,
        )
        self.assertEqual(self.request("/mcp", method="GET")[0], 405)

    def test_initialize_notification_and_tools_list(self) -> None:
        initialize = {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "initialize",
            "params": {
                "protocolVersion": "2025-11-25",
                "capabilities": {},
                "clientInfo": {"name": "unrealbridge-tests", "version": "1"},
            },
        }
        status, response, _ = self.request(
            "/mcp", method="POST", body=initialize, protocol=False
        )
        self.assertEqual(status, 200)
        self.assertEqual(response["result"]["protocolVersion"], "2025-11-25")
        self.assertEqual(response["result"]["serverInfo"]["name"], "UnrealBridge")
        self.assertEqual(response["result"]["serverInfo"]["version"], "3.0.0")

        notification = {"jsonrpc": "2.0", "method": "notifications/initialized"}
        self.assertEqual(
            self.request("/mcp", method="POST", body=notification)[0], 202
        )

        response = self.mcp(2, "tools/list", {})
        names = {tool["name"] for tool in response["result"]["tools"]}
        self.assertGreaterEqual(len(names), 40)
        self.assertIn("editor_op", names)
        self.assertIn("bridge_get_job", names)
        self.assertIn("bridge_list_official_toolsets", names)
        self.assertIn("bridge_describe_official_toolset", names)
        self.assertIn("bridge_submit_official_toolset_job", names)

    def test_official_read_only_job_and_unannotated_rejection(self) -> None:
        submitted = self.mcp(
            200,
            "tools/call",
            {
                "name": "bridge_submit_official_toolset_job",
                "arguments": {
                    "toolset": "UnrealBridge",
                    "tool": "RegistryHash",
                },
            },
        )
        self.assertFalse(submitted["result"]["isError"])
        job_id = submitted["result"]["structuredContent"]["job_id"]
        terminal = None
        for index in range(50):
            polled = self.mcp(
                210 + index,
                "tools/call",
                {"name": "bridge_get_job", "arguments": {"job_id": job_id}},
            )
            terminal = polled["result"]["structuredContent"]
            if terminal["terminal"]:
                break
            time.sleep(0.05)
        self.assertIsNotNone(terminal)
        self.assertEqual(terminal["job_state"], "succeeded")
        self.assertTrue(terminal["job_result"]["success"])

        rejected = self.mcp(
            300,
            "tools/call",
            {
                "name": "bridge_submit_official_toolset_job",
                "arguments": {
                    "toolset": "EditorToolset.LogsToolset",
                    "tool": "GetVerbosity",
                    "arguments": {"Category": "LogTemp"},
                },
            },
        )
        rejected_job = rejected["result"]["structuredContent"]["job_id"]
        rejected_terminal = None
        for index in range(50):
            polled = self.mcp(
                310 + index,
                "tools/call",
                {"name": "bridge_get_job", "arguments": {"job_id": rejected_job}},
            )
            rejected_terminal = polled["result"]["structuredContent"]
            if rejected_terminal["terminal"]:
                break
            time.sleep(0.05)
        self.assertIsNotNone(rejected_terminal)
        self.assertEqual(rejected_terminal["job_state"], "failed")
        self.assertIn(
            "no side-effect annotations",
            str(rejected_terminal["job_result"]["error"]).lower(),
        )

    def test_group_call_and_job_polling(self) -> None:
        submitted = self.mcp(
            10,
            "tools/call",
            {
                "name": "editor_op",
                "arguments": {
                    "operation": "get_compact_project_context_json",
                    "arguments": {},
                },
            },
        )
        job_id = submitted["result"]["structuredContent"]["job_id"]
        terminal = None
        for index in range(50):
            polled = self.mcp(
                20 + index,
                "tools/call",
                {"name": "bridge_get_job", "arguments": {"job_id": job_id}},
            )
            terminal = polled["result"]["structuredContent"]
            if terminal["terminal"]:
                break
            time.sleep(0.05)
        self.assertIsNotNone(terminal)
        self.assertEqual(terminal["job_state"], "succeeded")
        self.assertTrue(terminal["job_result"]["success"])

    def test_schema_rejection_happens_before_job_submission(self) -> None:
        response = self.mcp(
            100,
            "tools/call",
            {
                "name": "editor_op",
                "arguments": {
                    "operation": "get_compact_project_context_json",
                    "arguments": {"unexpected": True},
                },
            },
        )
        self.assertTrue(response["result"]["isError"])
        self.assertEqual(
            response["result"]["structuredContent"]["error_code"],
            "SCHEMA_VALIDATION_FAILED",
        )

    def test_rest_idempotency_returns_the_same_job(self) -> None:
        key = f"http-test-{uuid.uuid4()}"
        body = {
            "script": "print('http idempotency smoke')",
            "idempotency_key": key,
            "queue_timeout_seconds": 30,
        }
        first_status, first, _ = self.request(
            "/unrealbridge/jobs", method="POST", body=body
        )
        second_status, second, _ = self.request(
            "/unrealbridge/jobs", method="POST", body=body
        )
        self.assertEqual(first_status, 202)
        self.assertEqual(second_status, 202)
        self.assertEqual(first["job_id"], second["job_id"])
        self.assertTrue(second["deduplicated"])

    def test_polling_job_yields_between_editor_ticks(self) -> None:
        deadline = time.time() + 3.0
        body = {
            "script": "print('poll start')",
            "poll_script": (
                "import json, time\n"
                f"print(json.dumps({{'complete': time.time() >= {deadline!r}, "
                "'success': True, 'output': 'poll complete'}))"
            ),
            "poll_interval_seconds": 0.05,
            "run_timeout_seconds": 10,
            "idempotency_key": f"http-poll-{uuid.uuid4()}",
            "queue_timeout_seconds": 30,
        }
        status, submitted, _ = self.request(
            "/unrealbridge/jobs", method="POST", body=body
        )
        self.assertEqual(status, 202)
        job_id = submitted["job_id"]

        saw_running = False
        terminal = None
        for _ in range(100):
            status, snapshot, _ = self.request(f"/unrealbridge/jobs/{job_id}")
            self.assertEqual(status, 200)
            if snapshot["job_state"] == "running":
                saw_running = True
                health_status, health, _ = self.request("/unrealbridge/health")
                self.assertEqual(health_status, 200)
                self.assertEqual(health["running_job_id"], job_id)
            if snapshot["terminal"]:
                terminal = snapshot
                break
            time.sleep(0.02)

        self.assertTrue(saw_running)
        self.assertIsNotNone(terminal)
        self.assertEqual(terminal["job_state"], "succeeded")
        self.assertGreaterEqual(terminal["step_count"], 2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
