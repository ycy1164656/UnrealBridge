"""Executable scope/identity/revision failures; no Editor or Content required."""
from __future__ import annotations

import copy
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / ".claude/skills/unreal-bridge/scripts"))
import unreal_bridge_upgrade as upgrade


def request():
    return {"schema": upgrade.SCHEMA, "request_id": "scope-test-1", "operation_id": "upgrade.validate",
            "project_identity": "C:/dev/ShooterRoyal_5_8_DirectUpgrade/ShooterRoyal.uproject",
            "editor_session_id": "editor-1", "engine_version": "5.8.2-56702186+++UE5+Release-5.8",
            "target_packages": ["/ShooterRoyal/Automation/BridgeUpgrade/Audio/SC_SRUB_Authoring"],
            "expected_revisions": {"/ShooterRoyal/Automation/BridgeUpgrade/Audio/SC_SRUB_Authoring": "a" * 40}}


def snapshot():
    value = request()
    return {"ok": True, "project_identity": value["project_identity"],
            "editor_session_id": value["editor_session_id"], "engine_version": value["engine_version"],
            "world_handles": ["world-1"], "targets": {
                key: {"revision": revision, "dirty": False} for key, revision in value["expected_revisions"].items()}}


class UpgradeContractTests(unittest.TestCase):
    def rejects(self, value, expected, observed=None):
        with self.assertRaises(upgrade.UpgradeFault) as caught:
            upgrade.validate_request(value, snapshot() if observed is None else observed)
        self.assertEqual(expected, caught.exception.code)
        self.assertEqual("none", caught.exception.result()["side_effect_state"])

    def test_default_is_no_save_preview_and_input_is_not_changed(self):
        value = request()
        original = copy.deepcopy(value)
        result = upgrade.validate_request(value, snapshot())
        self.assertTrue(result["ok"])
        self.assertTrue(result["request"]["dry_run"])
        self.assertEqual("never", result["save_policy"])
        self.assertEqual(original, value)

    def test_canonical_order_has_stable_request_hash(self):
        original = request()
        reordered = dict(reversed(list(original.items())))
        self.assertEqual(upgrade.validate_request(original, snapshot())["request_hash"],
                         upgrade.validate_request(reordered, snapshot())["request_hash"])

    def test_changed_input_changes_hash(self):
        other = request()
        other["timeout_seconds"] = 20
        self.assertNotEqual(upgrade.validate_request(request(), snapshot())["request_hash"],
                            upgrade.validate_request(other, snapshot())["request_hash"])

    def test_different_project_is_rejected(self):
        value = request()
        value["project_identity"] = "C:/other/ShooterRoyal.uproject"
        self.rejects(value, "ScopeViolation")

    def test_windows_path_case_and_separators_are_equivalent(self):
        value = request()
        value["project_identity"] = value["project_identity"].upper().replace("/", "\\")
        self.assertTrue(upgrade.validate_request(value, snapshot())["ok"])

    def test_relative_project_and_project_traversal_fail(self):
        for path in ("ShooterRoyal", "../ShooterRoyal.uproject", "C:/dev/../p.uproject"):
            with self.subTest(path=path):
                value = request()
                value["project_identity"] = path
                self.rejects(value, "ScopeViolation")

    def test_editor_generation_changed(self):
        value = request()
        value["editor_session_id"] = "other-editor"
        self.rejects(value, "StaleHandle")

    def test_engine_version_changed(self):
        value = request()
        value["engine_version"] = "5.8.1"
        self.rejects(value, "EngineVersionMismatch")

    def test_world_membership_is_verified(self):
        value = request()
        value["world_handle"] = "world-1"
        self.assertTrue(upgrade.validate_request(value, snapshot())["ok"])
        value["world_handle"] = "stale-world"
        self.rejects(value, "StaleHandle")

    def test_revision_change_is_rejected(self):
        observed = snapshot()
        next(iter(observed["targets"].values()))["revision"] = "b" * 40
        self.rejects(request(), "TargetRevisionMismatch", observed)

    def test_snapshot_must_be_successful_and_exactly_scoped(self):
        self.rejects(request(), "NeedsReconciliation", {"ok": False})
        observed = snapshot()
        observed["targets"]["/Other/Asset"] = {"revision": "absent", "dirty": False}
        self.rejects(request(), "ScopeViolation", observed)

    def test_dirty_preview_allowed_but_mutation_is_rejected(self):
        observed = snapshot()
        next(iter(observed["targets"].values()))["dirty"] = True
        self.assertTrue(upgrade.validate_request(request(), observed)["ok"])
        value = request()
        value["dry_run"] = False
        self.rejects(value, "DirtyConflict", observed)

    def test_unknown_dirty_state_cannot_authorize_mutation(self):
        observed = snapshot()
        del next(iter(observed["targets"].values()))["dirty"]
        value = request()
        value["dry_run"] = False
        self.rejects(value, "DirtyConflict", observed)

    def test_implicit_save_is_rejected(self):
        value = request()
        value["save_policy"] = "all"
        self.rejects(value, "ScopeViolation")

    def test_unknown_operations_are_not_advertised_as_implemented(self):
        value = request()
        value["operation_id"] = "anim.apply_unimplemented_ops"
        self.rejects(value, "UnsupportedCapability")

    def test_unknown_fields_are_not_silently_ignored(self):
        value = request()
        value["python"] = "arbitrary code"
        self.rejects(value, "ValidationFailed")

    def test_path_scope_is_strict(self):
        for path in ("/Engine/Asset", "/Script/Engine", "/Game/../Asset", "/Game/A.A", "/Game/*", "/Game/A:B", "C:/Asset", "/Game\\A", "/Game/A\nB"):
            with self.subTest(path=path):
                value = request()
                value["target_packages"] = [path]
                value["expected_revisions"] = {path: "absent"}
                self.rejects(value, "ScopeViolation")

    def test_targets_must_have_exact_revisions(self):
        for revisions in ({}, {"/Game/Wrong": "absent"}, {request()["target_packages"][0]: "x"}):
            value = request()
            value["expected_revisions"] = revisions
            self.rejects(value, "TargetRevisionMismatch")

    def test_duplicate_and_excess_targets_rejected(self):
        for targets in (["/Game/A", "/game/a"], [f"/Game/A{i}" for i in range(17)]):
            value = request()
            value["target_packages"] = targets
            value["expected_revisions"] = {path: "absent" for path in targets}
            self.rejects(value, "ScopeViolation")

    def test_numbers_and_boolean_types_are_strict(self):
        for key, bad in (("timeout_seconds", True), ("timeout_seconds", float("nan")), ("timeout_seconds", float("inf")), ("timeout_seconds", 121), ("dry_run", 1), ("dry_run", "false")):
            with self.subTest(key=key, bad=bad):
                value = request()
                value[key] = bad
                self.rejects(value, "ValidationFailed")

    def test_request_and_identifier_budgets(self):
        value = request()
        value["request_id"] = "x" * 129
        self.rejects(value, "ValidationFailed")
        value["request_id"] = "x" * 70000
        self.rejects(value, "ValidationFailed")

    def test_native_payload_and_idempotency_are_preserved_by_adapter(self):
        from network_multiclient_v3_live_smoke import _load_server
        server = _load_server()
        with patch.object(server, "bridge_submit_job", return_value={"success": True}) as submit:
            server.bridge_submit_upgrade_validation(request(), project="ShooterRoyal")
        self.assertEqual("upgrade-validation:scope-test-1", submit.call_args.kwargs["idempotency_key"])
        self.assertEqual("ShooterRoyal", submit.call_args.kwargs["project"])
        self.assertEqual(30.0, submit.call_args.kwargs["run_timeout"])
        code = submit.call_args.args[0]
        self.assertIn("Upgrade.validate_upgrade_request", code)
        self.assertNotIn("no_preflight", submit.call_args.kwargs)
        with patch.object(server, "bridge_submit_job") as submit:
            invalid = request()
            invalid["save_policy"] = "all"
            self.assertFalse(server.bridge_submit_upgrade_validation(invalid)["ok"])
            submit.assert_not_called()

    def test_submit_uncertainty_never_becomes_safe_to_retry(self):
        from network_multiclient_v3_live_smoke import _load_server
        server = _load_server()
        with patch.object(server, "bridge_submit_job", return_value={"success": False, "error": "connection lost"}):
            result = server.bridge_submit_upgrade_validation(request())
        self.assertEqual("NeedsReconciliation", result["error_code"])
        self.assertEqual("unknown", result["side_effect_state"])
        self.assertFalse(result["retryable"])
        self.assertEqual("scope-test-1", result["request_id"])

    def test_id_conflict_is_rejected_without_claiming_effects(self):
        from network_multiclient_v3_live_smoke import _load_server
        server = _load_server()
        with patch.object(server, "bridge_submit_job", return_value={"success": False, "error": "idempotency key was already used with a different script"}):
            result = server.bridge_submit_upgrade_validation(request())
        self.assertEqual("ValidationFailed", result["error_code"])
        self.assertEqual("none", result["side_effect_state"])
        self.assertFalse(result["retryable"])


if __name__ == "__main__":
    unittest.main()
