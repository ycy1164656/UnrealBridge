"""Frozen content plans and evidence-based acceptance, integrated with existing Jobs.

No model loop, gameplay implementation, asset I/O or shell execution lives here.
The host supplies typed transport and the project supplies role/fixture bindings.
"""
from __future__ import annotations

import copy
import hashlib
import json
import math
import os
from pathlib import Path
import re
import threading
import time

from unreal_bridge_upgrade import canonical_bytes, package_name, project_identity
from unreal_bridge_workflows import ArtifactStore

KINDS = {'minion', 'tower', 'weapon', 'equipment', 'hero', 'ability', 'talent', 'animation', 'sfx', 'vfx', 'ui'}
STEP_FIELDS = {
    'create_table': {'source', 'target'},
    'duplicate_asset': {'source', 'target'},
    'import_audio': {'source_file', 'source_sha256', 'target'},
    'set_audio_routing': {'target', 'sound_class', 'submix', 'attenuation', 'concurrency'},
    'set_niagara_floats': {'target', 'values', 'expected_emitters'},
    'copy_table_row': {'source', 'source_row', 'target', 'target_row'},
    'set_table_fields': {'target', 'row', 'values'},
    'set_object_properties': {'target', 'values'},
    'compile': {'target'},
    'save': {'target'},
    'readback': {'target'},
}
STATES = {'pass', 'fail', 'inconclusive', 'blocked', 'not_run'}
_LOCK = threading.RLock()


def sha(value):
    return hashlib.sha256(canonical_bytes(value)).hexdigest()


def identifier(value):
    if not isinstance(value, str) or not re.fullmatch(r'[A-Za-z0-9_-]{1,96}', value):
        raise ValueError('Bounded work-order/content/assertion identifier required')
    return value


def strict(value, fields, required=()):
    if not isinstance(value, dict) or set(value) - set(fields) or set(required) - set(value):
        raise ValueError('Unknown or missing fields in typed contract')
    canonical_bytes(value)


def normalize_acceptance(value):
    strict(value, {'id', 'revision', 'assertions', 'source', 'golden_hash'}, {'id', 'revision', 'assertions', 'source'})
    identifier(value['id'])
    if not isinstance(value['source'], str) or not value['source'] or len(value['source']) > 2048:
        raise ValueError('Independent expectation source required')
    assertions = value['assertions']
    if not isinstance(assertions, list) or not 1 <= len(assertions) <= 64:
        raise ValueError('Acceptance requires 1..64 frozen assertions')
    seen = set()
    for assertion in assertions:
        strict(assertion, {'id', 'category', 'level', 'field', 'operator', 'expected', 'tolerance', 'trigger_path'},
               {'id', 'category', 'level', 'field', 'operator', 'expected'})
        identity = identifier(assertion['id'])
        if identity in seen:
            raise ValueError('Duplicate assertion identity')
        seen.add(identity)
        if assertion['category'] not in {'required', 'optional', 'human', 'deferred'}:
            raise ValueError('Invalid assertion category')
        if assertion['level'] not in {'L0', 'L1', 'L2', 'L3', 'L4', 'L5', 'LZ'}:
            raise ValueError('Invalid assertion level')
        if assertion['operator'] not in {'eq', 'gte', 'lte', 'contains', 'unique_logical_hits'}:
            raise ValueError('Unsupported assertion operator')
        if not re.fullmatch(r'[A-Za-z0-9_.-]{1,160}', assertion['field']):
            raise ValueError('Invalid observation field')
        tolerance = assertion.get('tolerance', 0)
        if type(tolerance) not in (float, int) or not math.isfinite(tolerance) or tolerance < 0:
            raise ValueError('Invalid frozen tolerance')
        if assertion['level'] in {'L2', 'L3'} and assertion.get('trigger_path') not in {'native_input', 'registered_ai', 'registered_gameplay'}:
            raise ValueError('Behavioral acceptance requires an explicit real trigger path')
        if assertion['category'] == 'deferred' and assertion['level'] != 'LZ':
            raise ValueError('Only the explicitly deferred stage Z is deferred by default')
        if assertion['category'] == 'human' and assertion['level'] != 'L5':
            raise ValueError('Human acceptance cannot be impersonated by machine evidence')
    return copy.deepcopy(value)


def normalize_recipe(recipe):
    required = {'schema', 'work_order_id', 'recipe_id', 'revision', 'project_identity', 'content_kind', 'content_id',
                'template_ref', 'template_fingerprint', 'target_packages', 'expected_revisions', 'bindings',
                'steps', 'acceptance_profile', 'protection', 'view_identity'}
    strict(recipe, required | {'budget', 'request_id'}, required)
    if recipe['schema'] != 'unrealbridge.content_recipe.v1':
        raise ValueError('Invalid content recipe schema')
    result = copy.deepcopy(recipe)
    for key in ('work_order_id', 'recipe_id', 'content_id'):
        identifier(result[key])
    result['project_identity'] = project_identity(result['project_identity'])
    if result['content_kind'] not in KINDS:
        raise ValueError('Unknown content kind')
    package_name(result['template_ref'])
    if not re.fullmatch(r'[a-f0-9]{40}|[a-f0-9]{64}', result['template_fingerprint']):
        raise ValueError('Fresh template fingerprint required')
    targets = result['target_packages']
    if not isinstance(targets, list) or not 1 <= len(targets) <= 16:
        raise ValueError('1..16 exact output packages required')
    for target in targets:
        package_name(target)
    if len({p.casefold() for p in targets}) != len(targets) or result['template_ref'] in targets:
        raise ValueError('Duplicate targets or template mutation prohibited')
    revisions = result['expected_revisions']
    if not isinstance(revisions, dict) or set(revisions) != set(targets):
        raise ValueError('Every target requires its initial revision')
    if any(not isinstance(v, str) or not re.fullmatch(r'absent|[a-f0-9]{40}|[a-f0-9]{64}', v) for v in revisions.values()):
        raise ValueError('Invalid target revision')
    if not isinstance(result['bindings'], dict) or not result['bindings'] or len(result['bindings']) > 32:
        raise ValueError('Resolved project role bindings required')
    if result['protection'] not in {'file_sandbox', 'declared_changeset'}:
        raise ValueError('Protection mode must be explicit; no silent fallback')
    strict(result['view_identity'], {'project', 'editor_session_id', 'sandbox_id', 'sandbox_generation', 'lease_id'},
           {'editor_session_id', 'sandbox_id', 'sandbox_generation'})
    if result['protection'] == 'file_sandbox' and not result['view_identity']['sandbox_id']:
        raise ValueError('Sandbox recipes require the actual active sandbox identity')
    steps = result['steps']
    if not isinstance(steps, list) or not 1 <= len(steps) <= 32:
        raise ValueError('1..32 typed steps required')
    seen = set()
    for step in steps:
        if not isinstance(step, dict) or step.get('operation') not in STEP_FIELDS:
            raise ValueError('Arbitrary script/console operations are not recipes')
        fields = STEP_FIELDS[step['operation']] | {'id', 'operation'}
        strict(step, fields, fields)
        if identifier(step['id']) in seen:
            raise ValueError('Duplicate step id')
        seen.add(step['id'])
        if step['target'] not in targets:
            raise ValueError('Step target outside the frozen write set')
        if 'source' in step:
            package_name(step['source'])
            if step['source'] not in {result['template_ref'], *targets}:
                raise ValueError('Unresolved source dependency')
        if step['operation'] == 'import_audio':
            if result['content_kind']!='sfx' or not isinstance(step['source_file'],str) or not Path(step['source_file']).is_absolute() or Path(step['source_file']).suffix.lower()!='.wav' or not re.fullmatch(r'[a-f0-9]{64}',step['source_sha256']):
                raise ValueError('SFX import requires an exact absolute WAVE and content hash')
            roots=result['bindings'].get('audio_staging_roots')
            if not isinstance(roots,list) or not 1<=len(roots)<=4 or not all(isinstance(p,str) and Path(p).is_absolute() for p in roots):
                raise ValueError('Explicit bounded audio staging roots required')
        if step['operation']=='set_audio_routing':
            for key in ('sound_class','submix','attenuation','concurrency'):
                if step[key]:package_name(step[key])
            if not step['sound_class']:raise ValueError('Actual SFX SoundClass is required')
        if step['operation']=='set_niagara_floats':
            if result['content_kind']!='vfx' or not isinstance(step['expected_emitters'],dict) or not 1<=len(step['expected_emitters'])<=32:
                raise ValueError('VFX requires exact expected emitter identities')
            for name,handle in step['expected_emitters'].items():
                if not isinstance(name,str) or not name or not re.fullmatch(r'[A-Fa-f0-9]{32}',handle):
                    raise ValueError('Stable emitter handle required')
            if not isinstance(step['values'],dict) or not step['values'] or any(type(v) not in (float,int) or not math.isfinite(v) or abs(v)>10000 for v in step['values'].values()):
                raise ValueError('Only bounded existing Niagara float defaults are supported')
        for key in ('row', 'source_row', 'target_row'):
            if key in step:
                identifier(step[key])
        if 'values' in step:
            if not isinstance(step['values'], dict) or not 1 <= len(step['values']) <= 32:
                raise ValueError('Bounded explicit property/field values required')
            for name, value in step['values'].items():
                if not re.fullmatch(r'[A-Za-z][A-Za-z0-9_]{0,95}', name) or not isinstance(value, (str, int, float, bool)):
                    raise ValueError('Only flat named scalar fields are supported')
                if isinstance(value, str) and len(value) > 2048:
                    raise ValueError('Field value exceeds budget')
    result['acceptance_profile'] = normalize_acceptance(result['acceptance_profile'])
    budget = result.setdefault('budget', {'repair_attempts': 2, 'restart_attempts': 1, 'evidence_bytes': 64 * 1024 * 1024})
    strict(budget, {'repair_attempts', 'restart_attempts', 'evidence_bytes'}, {'repair_attempts', 'restart_attempts', 'evidence_bytes'})
    for key, limit in [('repair_attempts', 2), ('restart_attempts', 1), ('evidence_bytes', 256 * 1024 * 1024)]:
        if type(budget[key]) is not int or not 0 <= budget[key] <= limit:
            raise ValueError('Invalid bounded ' + key)
    if len(canonical_bytes(result)) > 64 * 1024:
        raise ValueError('Recipe exceeds 64 KiB')
    return result


def atomic_json(path, value):
    """Durable intent precedes dispatch; this does not guarantee storage hardware power-loss behavior."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    data = canonical_bytes(value)
    temporary = path.with_suffix(path.suffix + '.pending')
    with temporary.open('wb') as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


class ProductionOrder:
    """A checkpoint/report for a Scenario/Job, not an alternative task scheduler."""
    def __init__(self, artifact_root, work_order_id):
        self.root = Path(artifact_root).resolve()
        self.identity = identifier(work_order_id)
        self.path = self.root / 'production' / (self.identity + '.json')
        self.artifacts = ArtifactStore(self.root)

    def get(self):
        state = json.loads(self.path.read_text(encoding='utf-8'))
        if sha(state['recipe']) != state['plan_hash'] or sha(state['recipe']['acceptance_profile']) != state['acceptance_hash']:
            raise ValueError('Persisted frozen plan/acceptance was modified')
        return state

    def freeze(self, recipe):
        normalized = normalize_recipe(recipe)
        if normalized['work_order_id'] != self.identity:
            raise ValueError('Work order identity mismatch')
        with _LOCK:
            plan_hash = sha(normalized)
            if self.path.exists():
                existing = self.get()
                if existing['plan_hash'] != plan_hash:
                    raise ValueError('Frozen plan/acceptance changed; new reviewed contract identity required')
                return existing
            report = {'schema': 'unrealbridge.production_order.v1', 'work_order_id': self.identity,
                      'plan_hash': plan_hash, 'acceptance_hash': sha(normalized['acceptance_profile']),
                      'recipe': normalized, 'status': 'planned', 'steps': {}, 'attempts': 0,
                      'side_effects': 'none', 'saved_packages': [], 'verification_runs': [],
                      'created_utc': time.time(), 'human_accepted': False, 'stage_z': 'deferred_by_user_resource_constraint'}
            atomic_json(self.path, report)
            return report

    def checkpoint(self, step_id, payload, phase, *, job_id=None, result=None):
        if phase not in {'intent', 'dispatched', 'confirmed', 'outcome_unknown','reconciled_not_applied'}:
            raise ValueError('Invalid dispatch phase')
        with _LOCK:
            state = self.get()
            expected = next((s for s in state['recipe']['steps'] if s['id'] == step_id), None)
            if expected != payload:
                raise ValueError('Step changed after plan freeze')
            prior = state['steps'].get(step_id)
            if prior and prior['payload_hash'] != sha(payload):
                raise ValueError('Idempotency payload conflict')
            if prior and prior['phase'] in {'dispatched', 'outcome_unknown'} and phase == 'intent':
                raise ValueError('Unknown dispatch must be reconciled, never replayed')
            if prior and prior['phase'] == 'confirmed':
                return state
            if phase == 'dispatched' and (not prior or prior['phase'] != 'intent' or not job_id):
                raise ValueError('Persisted intent and actual native Job required')
            if phase == 'confirmed' and (not prior or prior['phase'] not in {'dispatched', 'outcome_unknown'} or result is None):
                raise ValueError('Dispatched Job and actual result required')
            if phase=='reconciled_not_applied':
                if not prior or prior['phase']!='outcome_unknown' or not isinstance(result,dict) or result.get('phase')!='reconciled_not_applied':
                    raise ValueError('Native unchanged-preimage reconciliation receipt required')
                history=state.setdefault('reconciliations',[])
                if len(history)>=2:raise ValueError('Reconciliation budget exhausted')
                history.append({'step_id':step_id,'prior':prior,'native_evidence':result})
            state['steps'][step_id] = {'payload_hash': sha(payload), 'phase': phase,
                                      'job_id': job_id or (prior or {}).get('job_id'), 'result': result}
            state['side_effects'] = 'unknown' if phase in {'dispatched', 'outcome_unknown'} else 'observed'
            if phase == 'confirmed' and payload['operation'] == 'save' and result.get('ok'):
                state['saved_packages'] = sorted(set(state['saved_packages'] + [payload['target']]))
            state['status'] = 'needs_reconciliation' if phase == 'outcome_unknown' else phase
            atomic_json(self.path, state)
            return state

    def verify(self, observations, profile_hash, *, plan_hash, view_identity):
        with _LOCK:
            state = self.get()
            if state['plan_hash'] != plan_hash or state['acceptance_hash'] != profile_hash:
                raise ValueError('Frozen plan/acceptance hash changed')
            if state['recipe']['view_identity'] != view_identity:
                raise ValueError('Verification view mismatch')
            if observations.get('view_identity') != view_identity:
                raise ValueError('Observation came from a different or unidentified view')
            report = evaluate_acceptance(state['recipe']['acceptance_profile'], observations)
            report.update(plan_hash=plan_hash, acceptance_hash=profile_hash, view_identity=view_identity,
                          work_order_id=self.identity, run_index=len(state['verification_runs']))
            state['verification_runs'].append(report)
            state['status'] = report['status']
            atomic_json(self.path, state)
            return report

    def begin_repair(self, plan_hash, acceptance_hash):
        with _LOCK:
            state = self.get()
            if plan_hash != state['plan_hash'] or acceptance_hash != state['acceptance_hash']:
                raise ValueError('Repair cannot change the contract')
            if state['attempts'] >= state['recipe']['budget']['repair_attempts']:
                raise ValueError('Repair budget exhausted; preserve failures and report')
            if not state['verification_runs'] or state['verification_runs'][-1]['status'] == 'pass':
                raise ValueError('A failed verification is required before repair')
            state['attempts'] += 1
            atomic_json(self.path, state)
            return state

    def verify_registered(self, receipt, *, native_job_id, source_hash):
        """Internal transport path: caller JSON must never call this MCP branch directly."""
        with _LOCK:
            state=self.get()
            if not native_job_id or receipt.get('schema')!='unrealbridge.registered_evidence.v1' or not receipt.get('ok'):
                raise ValueError('Successful actual registered Job receipt required')
            observed=copy.deepcopy(receipt['observation'])
            if observed.get('plan_hash')!=state['plan_hash'] or observed.get('acceptance_hash')!=state['acceptance_hash'] or observed.get('adapter_source_sha256')!=source_hash:
                raise ValueError('Registered evidence contract/source mismatch')
            proof=observed.get('view_proof',{})
            if proof.get('original_view')!=state['recipe']['view_identity'] or proof.get('current_view')!=observed.get('view_identity'):
                raise ValueError('Original/current view proof mismatch')
            if proof.get('mode') not in {'original_saved_sandbox','persisted_main_sha1_verified','original_main_session'}:
                raise ValueError('Unproved view transition')
            if proof['mode']=='persisted_main_sha1_verified' and len(proof.get('targets',{}))!=len(state['recipe']['target_packages']):
                raise ValueError('Incomplete persisted view proof')
            observed['evidence_trust']='registered_native_job'
            report=evaluate_acceptance(state['recipe']['acceptance_profile'],observed)
            report.update(plan_hash=state['plan_hash'],acceptance_hash=state['acceptance_hash'],
                          view_identity=observed['view_identity'],view_proof=proof,native_job_id=native_job_id,
                          adapter_source_sha256=source_hash,work_order_id=self.identity,run_index=len(state['verification_runs']))
            state['verification_runs'].append(report);state['status']=report['status']
            atomic_json(self.path,state)
            return report


def _field(value, dotted):
    for part in dotted.split('.'):
        if not isinstance(value, dict) or part not in value:
            return None, False
        value = value[part]
    return value, True


def evaluate_acceptance(profile, observations):
    profile = normalize_acceptance(profile)
    if not isinstance(observations, dict) or len(canonical_bytes(observations)) > 256 * 1024:
        raise ValueError('Bounded structured observations required')
    results = []
    for assertion in profile['assertions']:
        row = {'id': assertion['id'], 'category': assertion['category'], 'level': assertion['level'], 'status': 'not_run'}
        if assertion['category'] in {'human', 'deferred'}:
            row['reason'] = 'human_acceptance_pending' if assertion['category'] == 'human' else 'stage_z_deferred'
            results.append(row)
            continue
        actual, found = _field(observations.get('values', {}), assertion['field'])
        if not found:
            row.update(status='not_run', reason='observation_missing')
        elif observations.get('dropped_events', 0) or observations.get('truncated'):
            row.update(status='inconclusive', reason='incomplete_observation_window')
        elif assertion['level'] in {'L2', 'L3'} and observations.get('trigger_path') != assertion.get('trigger_path'):
            row.update(status='blocked', reason='final_state_injection_is_not_real_trigger_path')
        else:
            expected, operation = assertion['expected'], assertion['operator']
            try:
                if operation == 'eq':
                    passed = abs(actual - expected) <= assertion.get('tolerance', 0) if type(actual) in (int, float) and type(expected) in (int, float) else type(actual) is type(expected) and actual == expected
                elif operation in {'gte', 'lte'}:
                    if type(actual) not in (int, float) or type(expected) not in (int, float):
                        raise ValueError('Numeric observation required')
                    passed = actual >= expected if operation == 'gte' else actual <= expected
                elif operation == 'contains':
                    passed = expected in actual
                else:
                    # Transport duplicates are identified by transport_id only. Distinct observations of
                    # the same action/hit/target/tick/role remain a real duplicate for this assertion.
                    transports, logical, passed = set(), set(), True
                    for hit in actual:
                        transport = hit['transport_id']
                        if transport in transports:
                            continue
                        transports.add(transport)
                        key = tuple(hit[k] for k in ('activation_id', 'logical_action_id', 'hit_id', 'target_id', 'tick_index', 'participant', 'role'))
                        if key in logical:
                            passed = False
                        logical.add(key)
                    passed = passed is expected
                row.update(status='pass' if passed else 'fail', actual=actual, expected=expected)
            except (TypeError, ValueError, KeyError):
                row.update(status='inconclusive', reason='invalid_observation_type_or_identity')
        results.append(row)
    required = [row['status'] for row in results if row['category'] == 'required']
    status = next((s for s in ('fail', 'blocked', 'inconclusive', 'not_run') if s in required), 'pass')
    if not required:
        status = 'not_run'
    return {'schema': 'unrealbridge.verification.v1', 'status': status, 'assertions': results,
            'human_accepted': False, 'evidence_trust': observations.get('evidence_trust', 'caller_provided_unverified'),
            'live_tested': observations.get('evidence_trust') == 'registered_native_job',
            'first_failure': next((r for r in results if r['category'] == 'required' and r['status'] != 'pass'), None)}
