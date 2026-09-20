import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / '.claude/skills/unreal-bridge/scripts'))
from unreal_bridge_production import ProductionOrder, normalize_recipe, evaluate_acceptance


def recipe():
    return {'schema': 'unrealbridge.content_recipe.v1', 'work_order_id': 'minion-one', 'recipe_id': 'minion',
            'revision': 1, 'project_identity': 'C:/project/game.uproject', 'content_kind': 'minion', 'content_id': 'variant-a',
            'template_ref': '/Game/Template', 'template_fingerprint': 'a' * 40, 'target_packages': ['/Game/Variant'],
            'expected_revisions': {'/Game/Variant': 'absent'}, 'bindings': {'row': 'Ranged'},
            'steps': [{'id': 'duplicate', 'operation': 'duplicate_asset', 'source': '/Game/Template', 'target': '/Game/Variant'},
                      {'id': 'save', 'operation': 'save', 'target': '/Game/Variant'}],
            'acceptance_profile': {'id': 'minion-acceptance', 'revision': 1, 'source': 'Approved minion gameplay contract',
                'assertions': [{'id': 'spawned', 'category': 'required', 'level': 'L2', 'field': 'spawned', 'operator': 'eq',
                                'expected': True, 'trigger_path': 'registered_ai'},
                               {'id': 'feel', 'category': 'human', 'level': 'L5', 'field': 'feel', 'operator': 'eq', 'expected': True}]},
            'protection': 'file_sandbox', 'view_identity': {'editor_session_id': 'editor-one', 'sandbox_id': 'sandbox-one',
                                                          'sandbox_generation': 1, 'lease_id': 'lease-one'}}


class ProductionTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.order = ProductionOrder(self.tmp.name, 'minion-one')
        self.plan = self.order.freeze(recipe())

    def observations(self, **changes):
        result = {'values': {'spawned': True}, 'trigger_path': 'registered_ai', 'dropped_events': 0,
                  'view_identity': self.plan['recipe']['view_identity']}
        result.update(changes)
        return result

    def verify(self, observed):
        return self.order.verify(observed, self.plan['acceptance_hash'], plan_hash=self.plan['plan_hash'],
                                 view_identity=self.plan['recipe']['view_identity'])

    def test_frozen_contract_cannot_change_or_be_tampered_on_disk(self):
        changed = recipe()
        changed['acceptance_profile']['assertions'][0]['expected'] = False
        with self.assertRaisesRegex(ValueError, 'Frozen'):
            self.order.freeze(changed)
        state = self.order.get()
        state['recipe']['acceptance_profile']['assertions'].pop(0)
        self.order.path.write_text(json.dumps(state), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'modified'):
            self.order.get()

    def test_complete_evidence_does_not_sign_human_acceptance_or_claim_live(self):
        result = self.verify(self.observations())
        self.assertEqual(result['status'], 'pass')
        self.assertEqual(result['assertions'][1]['status'], 'not_run')
        self.assertFalse(result['human_accepted'])
        self.assertFalse(result['live_tested'])

    def test_missing_events_fake_input_and_wrong_view_never_pass(self):
        self.assertEqual(self.verify(self.observations(dropped_events=1))['status'], 'inconclusive')
        self.assertEqual(self.verify(self.observations(values={}))['status'], 'not_run')
        self.assertEqual(self.verify(self.observations(trigger_path='set_health'))['status'], 'blocked')
        with self.assertRaisesRegex(ValueError, 'view'):
            self.verify(self.observations(view_identity={'sandbox_id': 'wrong'}))

    def test_dispatch_loss_cannot_be_replayed_and_native_confirmation_is_required(self):
        step = self.plan['recipe']['steps'][0]
        self.order.checkpoint('duplicate', step, 'intent')
        self.order.checkpoint('duplicate', step, 'dispatched', job_id='real-job')
        self.order.checkpoint('duplicate', step, 'outcome_unknown')
        with self.assertRaisesRegex(ValueError, 'reconciled'):
            self.order.checkpoint('duplicate', step, 'intent')
        self.order.checkpoint('duplicate', step, 'confirmed', result={'ok': True})
        self.assertEqual(self.order.get()['steps']['duplicate']['job_id'], 'real-job')

    def test_repair_budget_preserves_failures_and_contract(self):
        self.verify(self.observations(values={'spawned': False}))
        for _ in range(2):
            self.order.begin_repair(self.plan['plan_hash'], self.plan['acceptance_hash'])
        with self.assertRaisesRegex(ValueError, 'budget exhausted'):
            self.order.begin_repair(self.plan['plan_hash'], self.plan['acceptance_hash'])
        self.assertEqual(self.order.get()['verification_runs'][0]['status'], 'fail')

    def test_unknown_fields_nan_scope_and_arbitrary_scripts_rejected(self):
        for mutate in [lambda r: r.update(unknown=True), lambda r: r.update(template_fingerprint='wrong'),
                       lambda r: r['steps'][0].update(operation='exec'),
                       lambda r: r['steps'][0].update(target='/Engine/Protected'),
                       lambda r: r['acceptance_profile']['assertions'][0].update(tolerance=float('nan'))]:
            value = recipe()
            mutate(value)
            with self.assertRaises(ValueError):
                normalize_recipe(value)

    def test_logical_hit_duplicates_preserve_tick_and_role_semantics(self):
        profile = {'id': 'hits', 'revision': 1, 'source': 'Approved per-hit identity', 'assertions': [
            {'id': 'unique', 'category': 'required', 'level': 'L2', 'field': 'hits', 'operator': 'unique_logical_hits',
             'expected': True, 'trigger_path': 'registered_ai'}]}
        hit = {'transport_id': 'message-1', 'activation_id': 'a', 'logical_action_id': 'b', 'hit_id': 'c',
               'target_id': 'd', 'tick_index': 0, 'participant': 'server', 'role': 'authority'}
        observe = {'trigger_path': 'registered_ai', 'values': {'hits': [hit, dict(hit)]}}
        self.assertEqual(evaluate_acceptance(profile, observe)['status'], 'pass')
        observe['values']['hits'].append({**hit, 'transport_id': 'message-2', 'tick_index': 1})
        self.assertEqual(evaluate_acceptance(profile, observe)['status'], 'pass')
        observe['values']['hits'].append({**hit, 'transport_id': 'message-3'})
        self.assertEqual(evaluate_acceptance(profile, observe)['status'], 'fail')

    def test_registered_evidence_requires_source_contract_and_persisted_view_proof(self):
        view={'editor_session_id':'new-editor','sandbox_id':'','sandbox_generation':0}
        observed=self.observations(view_identity=view,plan_hash=self.plan['plan_hash'],acceptance_hash=self.plan['acceptance_hash'],
            adapter_source_sha256='a'*64,view_proof={'original_view':self.plan['recipe']['view_identity'],
            'current_view':view,'mode':'persisted_main_sha1_verified','targets':{'real.uasset':'b'*40}})
        receipt={'ok':True,'schema':'unrealbridge.registered_evidence.v1','observation':observed}
        report=self.order.verify_registered(receipt,native_job_id='actual-job',source_hash='a'*64)
        self.assertEqual(report['status'],'pass');self.assertTrue(report['live_tested']);self.assertFalse(report['human_accepted'])
        for key,value in [('plan_hash','x'),('adapter_source_sha256','b'*64),('view_proof',{})]:
            bad=copy.deepcopy(receipt);bad['observation'][key]=value
            with self.assertRaises(ValueError):self.order.verify_registered(bad,native_job_id='actual-job',source_hash='a'*64)

    def test_reconciled_attempt_retains_failed_job_and_does_not_change_expectations(self):
        step=self.plan['recipe']['steps'][0]
        self.order.checkpoint(step['id'],step,'intent')
        self.order.checkpoint(step['id'],step,'dispatched',job_id='first-job')
        self.order.checkpoint(step['id'],step,'outcome_unknown')
        self.order.checkpoint(step['id'],step,'reconciled_not_applied',result={'phase':'reconciled_not_applied'})
        self.order.checkpoint(step['id'],step,'intent')
        state=self.order.get()
        self.assertEqual(state['reconciliations'][0]['prior']['job_id'],'first-job')
        self.assertEqual(state['acceptance_hash'],self.plan['acceptance_hash'])


if __name__ == '__main__':
    unittest.main()
