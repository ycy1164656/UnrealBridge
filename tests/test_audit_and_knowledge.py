#!/usr/bin/env python3
"""Offline tests for the audit/organize layer and the local knowledge stores.

These exercise the contracts that keep the new surfaces honest: truncation is
reported, a move plan cannot be applied against changed evidence, a verified
fragment does not stay verified across an environment change, and friction
records are deduplicated, capped and scrubbed.
"""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / ".claude" / "skills" / "unreal-bridge" / "scripts"))

import unreal_bridge_audit as audit  # noqa: E402
import unreal_bridge_knowledge as knowledge  # noqa: E402


class FakeEditor:
    """Minimal stand-in for the official ReadOnly/TransactionalSync tools."""

    def __init__(self, assets=None, referencers=None, dependencies=None, classes=None):
        self.assets = assets or {}
        self.referencers = referencers or {}
        self.dependencies = dependencies or {}
        self.classes = classes or {}
        self.moves = []
        self.fail_move_for = set()

    def __call__(self, *, toolset, tool, arguments):
        if tool == "find_assets":
            return list(self.assets.get(arguments["path"], []))
        if tool == "get_referencers":
            return list(self.referencers.get(arguments["asset_path"], []))
        if tool == "get_dependencies":
            return list(self.dependencies.get(arguments["asset_path"], []))
        if tool == "get_asset_tags":
            return {}
        if tool == "get_asset_class":
            return self.classes.get(arguments["asset_path"], "Blueprint")
        if tool == "move":
            if arguments["source_path"] in self.fail_move_for:
                return {"success": False, "error": "editor refused the move"}
            self.moves.append((arguments["source_path"], arguments["destination_path"]))
            return {"success": True}
        raise AssertionError(f"unexpected tool {tool}")


class AuditTests(unittest.TestCase):
    def test_scope_must_be_content_path(self):
        with self.assertRaises(audit.AuditError):
            audit.collect_assets(FakeEditor(), ["C:/dev/whatever"])

    def test_truncation_is_reported_not_hidden(self):
        editor = FakeEditor(assets={"/Game/X": [f"/Game/X/A{i}" for i in range(10)]})
        found, truncated = audit.collect_assets(editor, ["/Game/X"], max_assets=4)
        self.assertEqual(len(found), 4)
        self.assertTrue(truncated)

    def test_orphan_and_heavy_dependency_findings_carry_evidence(self):
        editor = FakeEditor(
            assets={"/Game/X": ["/Game/X/BP_Orphan", "/Game/X/BP_Heavy"]},
            referencers={"/Game/X/BP_Orphan": [], "/Game/X/BP_Heavy": ["/Game/X/BP_Orphan"]},
            dependencies={"/Game/X/BP_Orphan": [], "/Game/X/BP_Heavy": [f"/Game/D{i}" for i in range(200)]},
        )
        result = audit.audit_project(editor, ["/Game/X"])
        self.assertEqual(result["mutations"], "none; this aggregation is read-only by construction")

        by_category = {f["category"] for f in result["findings"]}
        self.assertIn(audit.CATEGORY_ORGANIZATION, by_category)
        self.assertIn(audit.CATEGORY_PERFORMANCE_LEAD, by_category)

        heavy = [f for f in result["findings"] if f["category"] == audit.CATEGORY_PERFORMANCE_LEAD][0]
        self.assertEqual(heavy["severity"], "high")
        self.assertIn("basis", heavy["evidence"])
        # A dependency count is a lead, and must not be phrased as a measured cost.
        self.assertIn("not a confirmed cost", heavy["evidence"]["basis"])

    def test_orphan_finding_states_its_scope_limit(self):
        editor = FakeEditor(
            assets={"/Game/X": ["/Game/X/BP_Orphan"]},
            referencers={"/Game/X/BP_Orphan": []},
            dependencies={"/Game/X/BP_Orphan": []},
        )
        result = audit.audit_project(editor, ["/Game/X"])
        orphan = [f for f in result["findings"] if f["evidence"].get("referencer_count") == 0][0]
        self.assertIn("outside the scope", orphan["evidence"]["basis"])

    def test_naming_audit_uses_supplied_rules(self):
        editor = FakeEditor(
            assets={"/Game/X": ["/Game/X/BadName", "/Game/X/BP_Good"]},
            classes={"/Game/X/BadName": "Blueprint", "/Game/X/BP_Good": "Blueprint"},
        )
        result = audit.naming_audit(editor, ["/Game/X"])
        self.assertEqual(result["violation_count"], 1)
        self.assertEqual(result["violations"][0]["asset"], "/Game/X/BadName")
        self.assertEqual(result["mutations"], "none")

    def test_unknown_category_or_severity_is_refused(self):
        with self.assertRaises(audit.AuditError):
            audit.Finding(category="invented", severity="high", summary="x", asset="/Game/A")
        with self.assertRaises(audit.AuditError):
            audit.Finding(category=audit.CATEGORY_ORGANIZATION, severity="urgent", summary="x", asset="/Game/A")


class OrganizeTests(unittest.TestCase):
    def _editor(self):
        return FakeEditor(referencers={"/Game/A/Thing": ["/Game/B/User"]})

    def test_plan_blocks_traversal_and_foreign_roots(self):
        editor = self._editor()
        result = audit.plan_moves(editor, [
            {"source": "/Game/A/Thing", "destination": "/Engine/Evil/Thing"},
            {"source": "/Game/A/../../etc/passwd", "destination": "/Game/B/Thing"},
        ])
        self.assertFalse(result["ok"])
        self.assertEqual(len(result["blocked"]), 2)
        self.assertEqual(result["move_count"], 0)

    def test_plan_reports_expected_redirectors(self):
        result = audit.plan_moves(self._editor(), [
            {"source": "/Game/A/Thing", "destination": "/Game/B/Thing"},
        ])
        self.assertTrue(result["ok"])
        self.assertTrue(result["plan"]["entries"][0]["redirector_expected"])
        self.assertEqual(result["mutations"], "none; this is a plan, not an execution")

    def test_apply_requires_explicit_confirm(self):
        editor = self._editor()
        plan = audit.plan_moves(editor, [{"source": "/Game/A/Thing", "destination": "/Game/B/Thing"}])
        with self.assertRaises(audit.AuditError):
            audit.apply_moves(editor, plan["plan"], plan["plan_digest"])
        self.assertEqual(editor.moves, [])

    def test_apply_refuses_a_stale_plan(self):
        editor = self._editor()
        plan = audit.plan_moves(editor, [{"source": "/Game/A/Thing", "destination": "/Game/B/Thing"}])
        # Someone else referenced the asset after the plan was reviewed.
        editor.referencers["/Game/A/Thing"] = ["/Game/B/User", "/Game/C/NewUser"]
        result = audit.apply_moves(editor, plan["plan"], plan["plan_digest"], confirm=True)
        self.assertFalse(result["ok"])
        self.assertEqual(result["error"], "plan_stale")
        self.assertEqual(editor.moves, [], "a stale plan must not move anything")

    def test_apply_executes_a_fresh_plan_without_saving(self):
        editor = self._editor()
        plan = audit.plan_moves(editor, [{"source": "/Game/A/Thing", "destination": "/Game/B/Thing"}])
        result = audit.apply_moves(editor, plan["plan"], plan["plan_digest"], confirm=True)
        self.assertTrue(result["ok"])
        self.assertEqual(editor.moves, [("/Game/A/Thing", "/Game/B/Thing")])
        self.assertFalse(result["packages_saved"])

    def test_apply_stops_at_first_failure(self):
        editor = FakeEditor(referencers={"/Game/A/One": [], "/Game/A/Two": []})
        editor.fail_move_for.add("/Game/A/One")
        plan = audit.plan_moves(editor, [
            {"source": "/Game/A/One", "destination": "/Game/B/One"},
            {"source": "/Game/A/Two", "destination": "/Game/B/Two"},
        ])
        result = audit.apply_moves(editor, plan["plan"], plan["plan_digest"], confirm=True)
        self.assertFalse(result["ok"])
        self.assertEqual(editor.moves, [], "later entries must not run after a failure")
        self.assertEqual(result["remaining"], 1)


class FragmentCatalogTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        self.env = knowledge.Environment("5.8.2", "3.2.1", "abc123")

    def tearDown(self):
        self._tmp.cleanup()

    def test_verified_entry_goes_stale_when_environment_changes(self):
        catalog = knowledge.FragmentCatalog(self.root)
        catalog.add(
            "Sprint Combo", "BEGIN OBJECT...",
            source_blueprint="/Game/GA_Sprint", source_graph="EventGraph",
            environment=self.env, status=knowledge.STATUS_VERIFIED,
        )
        same = catalog.query(self.env)
        self.assertEqual(same["results"][0]["effective_status"], knowledge.STATUS_VERIFIED)

        upgraded = knowledge.Environment("5.8.3", "3.2.1", "abc123")
        later = catalog.query(upgraded)
        self.assertEqual(later["results"][0]["effective_status"], knowledge.STATUS_STALE)
        # The stored record keeps saying what was actually verified, and when.
        self.assertEqual(later["results"][0]["stored_status"], knowledge.STATUS_VERIFIED)

    def test_incomplete_environment_cannot_claim_verified(self):
        catalog = knowledge.FragmentCatalog(self.root)
        partial = knowledge.Environment("5.8.2", "", "")
        catalog.add(
            "Partial", "TEXT",
            source_blueprint="/Game/BP", source_graph="EventGraph",
            environment=partial, status=knowledge.STATUS_VERIFIED,
        )
        result = catalog.query(partial)
        self.assertEqual(result["results"][0]["effective_status"], knowledge.STATUS_STALE)

    def test_require_verified_filters_stale_entries(self):
        catalog = knowledge.FragmentCatalog(self.root)
        catalog.add("A", "T", source_blueprint="/Game/A", source_graph="G",
                    environment=self.env, status=knowledge.STATUS_VERIFIED)
        other = knowledge.Environment("5.9.0", "3.2.1", "abc123")
        self.assertEqual(catalog.query(other, require_verified=True)["result_count"], 0)

    def test_index_is_rebuildable_from_entries(self):
        catalog = knowledge.FragmentCatalog(self.root)
        catalog.add("A", "T", source_blueprint="/Game/A", source_graph="G", environment=self.env)
        index_path = self.root / "index.json"
        catalog.rebuild_index()
        self.assertTrue(index_path.is_file())
        index_path.unlink()
        rebuilt = catalog.rebuild_index()
        self.assertEqual(rebuilt["entry_count"], 1)

    def test_empty_fragment_is_refused(self):
        catalog = knowledge.FragmentCatalog(self.root)
        with self.assertRaises(knowledge.KnowledgeError):
            catalog.add("Empty", "   ", source_blueprint="/Game/A", source_graph="G",
                        environment=self.env)


class FrictionTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.log = knowledge.FrictionLog(Path(self._tmp.name))

    def tearDown(self):
        self._tmp.cleanup()

    def test_repeated_friction_is_deduplicated_with_a_count(self):
        for _ in range(3):
            result = self.log.record("missing_tool", "No reimport entry point", attempted="searched catalog")
        self.assertTrue(result["deduplicated"])
        self.assertEqual(result["occurrences"], 3)
        self.assertEqual(self.log.report()["record_count"], 1)

    def test_user_paths_are_scrubbed(self):
        self.log.record(
            "error", "Import failed",
            evidence=r"could not read C:\Users\someone\Desktop\icon.png",
        )
        stored = json.loads((Path(self._tmp.name) / "friction.json").read_text(encoding="utf-8"))
        evidence = stored["records"][0]["evidence"]
        self.assertNotIn("someone", evidence)
        self.assertIn("<user-home>", evidence)

    def test_posix_home_is_scrubbed(self):
        self.log.record("error", "x", evidence="/home/someone/project/a.png")
        stored = json.loads((Path(self._tmp.name) / "friction.json").read_text(encoding="utf-8"))
        self.assertNotIn("someone", stored["records"][0]["evidence"])

    def test_log_is_capped(self):
        for i in range(knowledge.MAX_FRICTION_RECORDS + 20):
            self.log.record("noise", f"distinct issue {i}")
        self.assertLessEqual(self.log.report()["record_count"], knowledge.MAX_FRICTION_RECORDS)

    def test_report_ranks_by_frequency(self):
        self.log.record("a", "rare issue")
        for _ in range(5):
            self.log.record("b", "common issue")
        records = self.log.report()["records"]
        self.assertEqual(records[0]["summary"], "common issue")

    def test_summary_is_required(self):
        with self.assertRaises(knowledge.KnowledgeError):
            self.log.record("kind", "   ")


class PolicyRaiseTests(unittest.TestCase):
    """The texture import raise must stay narrow and keep its guard wired."""

    @classmethod
    def setUpClass(cls):
        cls.policy = json.loads(
            (REPO / "Plugin" / "UnrealBridge" / "Resources" / "ue58_official_tool_policy.json")
            .read_text(encoding="utf-8")
        )

    def test_texture_import_is_raised_with_a_declared_source_argument(self):
        entry = self.policy["tools"]["editor_toolset.toolsets.texture.TextureTools|import_file"]
        self.assertEqual(entry["access"], "TransactionalSync")
        self.assertEqual(entry["external_source_arg"], "source_file")
        self.assertTrue(entry["requires_explicit_targets"])
        self.assertEqual(entry["save_behavior"], "Never")

    def test_other_import_backends_remain_rejected(self):
        for key, entry in self.policy["tools"].items():
            if key.endswith("|import_file") and "texture.TextureTools" not in key:
                self.assertEqual(entry["access"], "Rejected", key)

    def test_destructive_delete_remains_rejected(self):
        entry = self.policy["tools"]["editor_toolset.toolsets.asset.AssetTools|delete"]
        self.assertEqual(entry["access"], "Rejected")

    def test_policy_stays_deny_by_default(self):
        self.assertTrue(self.policy["deny_by_default"])

    def test_only_declared_tools_carry_an_external_source_argument(self):
        with_source = {
            key for key, entry in self.policy["tools"].items() if entry.get("external_source_arg")
        }
        self.assertEqual(
            with_source,
            {"editor_toolset.toolsets.texture.TextureTools|import_file"},
        )

    def test_source_audit_findings_are_present(self):
        # Regenerating without --toolsets-source-root silently drops these.
        self.assertGreater(self.policy["source_audit"]["explicit_disk_save_operation_count"], 0)


if __name__ == "__main__":
    unittest.main()
