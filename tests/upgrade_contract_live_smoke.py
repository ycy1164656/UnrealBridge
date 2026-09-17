"""Read-only native/host contract and empty guarded-transaction acceptance.

No asset is created, modified, saved, undone or reloaded. Poll sleeps happen on
the host. Existing JobManager owns timeout, cancellation and idempotency.
"""
from __future__ import annotations

import argparse
import copy
import json
import time
import uuid
from pathlib import Path

from network_multiclient_v3_live_smoke import _load_server


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    server = _load_server()
    report = {"assertions": [], "content_writes": 0, "undo_calls": 0}
    nonce = uuid.uuid4().hex

    def check(name, condition, detail=None):
        assert condition, {"assertion": name, "detail": detail}
        report["assertions"].append(name)

    def execute(code):
        response = server.bridge_exec(code, project=args.project)
        assert response.get("success"), response
        value = server._last_json_output(response)
        assert isinstance(value, dict), response
        return value

    def wait(job_id):
        deadline = time.monotonic() + 40
        while time.monotonic() < deadline:
            state = server.bridge_wait_job(job_id, project=args.project, wait_timeout=5)
            if state.get("terminal"):
                return state
        raise TimeoutError(f"Job did not terminate: {job_id}")

    def native(request):
        payload = json.dumps(request, allow_nan=False, ensure_ascii=False)
        return execute("from unreal_bridge import Upgrade\n"
                       f"print(Upgrade.validate_upgrade_request(request_json={payload!r}))")

    def editor_state():
        return execute("import json\nfrom unreal_bridge import Editor\n"
                       "state=Editor.get_editor_state()\n"
                       "print(json.dumps({'pie':state.is_pie,"
                       "'dirty':sorted(Editor.get_dirty_package_names())}))")

    try:
        report["before"] = editor_state()
        check("Editor remains available without PIE", not report["before"]["pie"])
        snapshot = execute("from unreal_bridge import Upgrade\n"
                           "print(Upgrade.get_authoring_snapshot(target_packages_json='[]'))")
        request = {key: snapshot[key] for key in
                   ("schema", "project_identity", "editor_session_id", "engine_version")}
        request.update(request_id="live-" + nonce, operation_id="upgrade.validate",
                       target_packages=[], expected_revisions={})
        report["context"] = snapshot
        good = native(request)
        check("Native current context validates", good.get("ok"), good)
        submitted = server.bridge_submit_upgrade_validation(request, project=args.project)
        check("Host validation submits durable job", submitted.get("success"), submitted)
        job = wait(submitted["job_id"])
        check("Host job executes native guard", server._last_json_output(job).get("ok"), job)
        check("Host/native input digest agrees", submitted.get("input_digest") ==
              server._last_json_output(job).get("input_digest"), job)
        repeated = server.bridge_submit_upgrade_validation(request, project=args.project)
        check("Identical request resolves same job", repeated.get("deduplicated") and
              repeated.get("job_id") == submitted["job_id"], repeated)
        different = dict(request, timeout_seconds=20)
        conflict = server.bridge_submit_upgrade_validation(different, project=args.project)
        check("Same ID with different input is refused", not conflict.get("success") and
              conflict.get("error_code") == "ValidationFailed" and
              "different script" in conflict.get("error", ""), conflict)

        cases = [("project_identity", "C:/unrelated/test.uproject", "ScopeViolation"),
                 ("editor_session_id", "expired", "StaleHandle"),
                 ("engine_version", "5.8.0", "EngineVersionMismatch"),
                 ("world_handle", "expired", "StaleHandle"),
                 ("save_policy", "all", "ScopeViolation"),
                 ("dry_run", "false", "ValidationFailed"),
                 ("timeout_seconds", True, "ValidationFailed"),
                 ("timeout_seconds", 121, "ValidationFailed"),
                 ("operation_id", "not.implemented", "UnsupportedCapability"),
                 ("unexpected", 1, "ValidationFailed")]
        for index, (key, value, expected) in enumerate(cases):
            item = copy.deepcopy(request)
            item.update({key: value, "request_id": f"{nonce}-{index}"})
            result = native(item)
            check(f"Native rejects {key}={value!r}", not result.get("ok") and
                  result.get("error_code") == expected, result)
            host = server.bridge_submit_upgrade_validation(item, project=args.project)
            if host.get("success") and host.get("job_id"):
                host = server._last_json_output(wait(host["job_id"]))
            check(f"Host/native error agrees for {key}={value!r}",
                  host.get("error_code") == expected, host)

        target = "/ShooterRoyal/Automation/BridgeUpgrade/Contract/Absent_" + nonce
        item = dict(request, request_id=nonce + "-absent", target_packages=[target],
                    expected_revisions={target: "absent"})
        check("Absent target is inspectable without creation", native(item).get("ok"))
        item["expected_revisions"] = {target: "a" * 40}
        result = native(item)
        check("Revision drift is refused", result.get("error_code") == "TargetRevisionMismatch", result)

        for failure in (False, True):
            script = ("import json\nfrom unreal_bridge import ChangeSet\n"
                      f"cs=ChangeSet.begin_guarded_change_set(name='SRUB empty retention probe',target_packages=[{target!r}])\n"
                      "assert cs\nprint(json.dumps({'change_set_id':cs}))\n")
            if failure:
                script += "raise RuntimeError('SRUB deliberate empty-job failure probe')\n"
            response = server.bridge_exec(script, project=args.project)
            result = server._last_json_output(response)
            assert result and result.get("change_set_id"), response
            cs = result["change_set_id"]
            preview = execute("import json\nfrom unreal_bridge import ChangeSet\n"
                              f"p=ChangeSet.preview_change_set(change_set_id={cs!r})\n"
                              "print(json.dumps({'status':p.status,'saved':p.saved,"
                              "'retain':p.retain_on_failure,'rollback':p.rollback_verified,"
                              "'dirty':list(p.dirty_packages_for_job),'error':p.error}))")
            check(f"Guarded {'failed' if failure else 'uncommitted'} job retains without Undo",
                  preview["status"] == "NeedsReconciliation" and preview["retain"] and
                  not preview["saved"] and not preview["rollback"] and not preview["dirty"], preview)

        polling = server.bridge_submit_job(code="print('{}')",
            poll_code="print('{\"complete\": false}')", poll_interval=0.1,
            run_timeout=30, project=args.project)
        assert polling.get("success"), polling
        uncertain = server.bridge_wait_job(polling["job_id"], wait_timeout=0.1, project=args.project)
        check("Wait timeout does not imply job failure or completion", not uncertain.get("terminal"), uncertain)
        current = server.bridge_get_job(polling["job_id"], project=args.project)
        check("Unknown wait result reconciles with original job", current.get("job_id") == polling["job_id"] and
              not current.get("terminal"), current)
        cancel = server.bridge_cancel_job(polling["job_id"], project=args.project)
        check("Cancellation request accepted", cancel.get("success"), cancel)
        cancelled = wait(polling["job_id"])
        check("Cancellation completion is separately observed", cancelled.get("job_state") == "cancelled", cancelled)
        report["after"] = editor_state()
        check("No Dirty or PIE changes", report["before"] == report["after"], report["after"])
        report["result"] = "SRUB_CONTRACT_LIVE_PASS"
    except Exception as error:
        report["result"] = "SRUB_CONTRACT_LIVE_FAIL"
        report["error"] = str(error)
        raise
    finally:
        Path(args.out).parent.mkdir(parents=True, exist_ok=True)
        Path(args.out).write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({"result": report["result"], "assertions": len(report["assertions"]),
                          "out": args.out}, ensure_ascii=False), flush=True)


if __name__ == "__main__":
    main()
