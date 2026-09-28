from test_audit_and_knowledge import FakeEditor
import unittest
import unreal_bridge_audit as a


class MoveIntegrityTests(unittest.TestCase):
    def setUp(self):
        self.editor = FakeEditor(referencers={'/Game/A/Thing':['/Game/User']})
        self.plan = a.plan_moves(self.editor, [{'source':'/Game/A/Thing','destination':'/Game/B/Thing'}])

    def apply(self):
        return a.apply_moves(self.editor, self.plan['plan'], self.plan['plan_digest'], confirm=True)

    def test_same_count_changed_identity_is_stale(self):
        self.editor.referencers['/Game/A/Thing'] = ['/Game/OtherUser']
        self.assertEqual(self.apply()['error'], 'plan_stale')
        self.assertEqual(self.editor.moves, [])

    def test_source_revision_changed(self):
        self.editor.revisions['/Game/A/Thing'] = 'edited'
        self.assertEqual(self.apply()['error'], 'plan_stale')

    def test_destination_appeared(self):
        self.editor.destinations.add('/Game/B/Thing')
        self.assertEqual(self.apply()['error'], 'plan_stale')

    def test_dependency_identity_changed(self):
        self.editor.dependencies['/Game/A/Thing'] = ['/Game/NewDependency']
        self.assertEqual(self.apply()['error'], 'plan_stale')

    def test_tampered_plan_rejected(self):
        self.plan['plan']['entries'][0]['destination'] = '/Game/Evil'
        with self.assertRaises(a.AuditError):
            self.apply()

    def test_case_collision_and_chain_rejected(self):
        for moves in ([{'source':'/Game/A','destination':'/Game/a'}],
                      [{'source':'/Game/A','destination':'/Game/B'}, {'source':'/Game/B','destination':'/Game/C'}]):
            with self.assertRaises(a.AuditError):
                a.plan_moves(self.editor,moves)

    def test_missing_policy_never_uses_generic_prefix_as_violation(self):
        editor = FakeEditor(assets={'/Game/Test':['/Game/Test/Anything']})
        result = a.naming_audit(editor, ['/Game/Test'])
        self.assertEqual(result['rule_status'], 'project_rule_missing')
        self.assertEqual(result['violation_count'], 0)

    def test_failed_referencer_readback_stops_after_move(self):
        self.editor.move_readback=lambda entry:{'ok':False,'error':'broken reference'}
        result=self.apply()
        self.assertFalse(result['ok'])
        self.assertEqual(result['side_effect_state'],'needs_reconciliation')
        self.assertEqual(len(self.editor.moves),1)


if __name__ == '__main__':
    unittest.main()
