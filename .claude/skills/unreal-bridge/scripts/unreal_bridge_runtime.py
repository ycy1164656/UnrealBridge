"""Typed, ownership-scoped runtime acceptance with durable evidence and no Content writes."""
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
import uuid

SCHEMA = 'unrealbridge.runtime_recipe.v1'
ACTIVE = {'queued', 'running'}
FIELDS = {'world_count', 'has_begun_play', 'controller_available', 'pawn_available', 'actor_count', 'shader_jobs', 'asset_jobs'}


class RuntimeFault(RuntimeError):
    pass


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':'), ensure_ascii=False, allow_nan=False).encode('utf-8')).hexdigest()


def normalize(spec):
    if not isinstance(spec, dict) or spec.get('schema') != SCHEMA:
        raise ValueError('Expected unrealbridge.runtime_recipe.v1')
    if set(spec) - {'schema', 'name', 'mode', 'client_count', 'timeout_seconds', 'ready_timeout_seconds', 'steps'}:
        raise ValueError('Unknown runtime recipe fields; scripts/console/asset writes are not accepted')
    result = copy.deepcopy(spec)
    mode = result.setdefault('mode', 'observe')
    if mode not in ('observe', 'owned_pie'):
        raise ValueError('mode must be observe or owned_pie')
    count = result.setdefault('client_count', 1)
    if type(count) is not int or count not in (1, 3):
        raise ValueError('client_count must be 1 or 3 (listen server plus two clients)')
    result.setdefault('timeout_seconds', 120)
    result.setdefault('ready_timeout_seconds', 90)
    for key, lo, hi in [('timeout_seconds', 5, 300), ('ready_timeout_seconds', 1, 120)]:
        value = result[key]
        if type(value) not in (int, float) or not math.isfinite(value) or not lo <= value <= hi:
            raise ValueError(f'{key} must be {lo}..{hi}')
    steps = result.setdefault('steps', [])
    if not isinstance(steps, list) or len(steps) > 32:
        raise ValueError('At most 32 typed steps are allowed')
    seen = set()
    for step in steps:
        if not isinstance(step, dict) or not re.fullmatch(r'[A-Za-z0-9_-]{1,64}', str(step.get('id', ''))) or step['id'] in seen:
            raise ValueError('Each step requires a unique short id')
        seen.add(step['id'])
        kind = step.get('type')
        if kind not in ('assert', 'wait', 'input'):
            raise ValueError('Step type must be assert, wait or input')
        allowed = {'id', 'type', 'world', 'field', 'op', 'value', 'timeout_seconds'} if kind != 'input' else {'id', 'type', 'world', 'input_action_path', 'value'}
        if set(step) - allowed:
            raise ValueError('Unknown step fields')
        selector = step.get('world')
        if selector is not None:
            if not isinstance(selector, dict) or not selector or set(selector) - {'world_handle', 'net_mode', 'pie_instance', 'world_type'}:
                raise ValueError('Invalid World selector')
            if any(type(v) not in (str, int) for v in selector.values()):
                raise ValueError('Invalid World selector value')
        if kind == 'input':
            if mode != 'owned_pie' or not selector:
                raise ValueError('Semantic input requires owned_pie and an explicit unambiguous World selector')
            path = step.get('input_action_path', '')
            if not isinstance(path, str) or not re.fullmatch(r'/ShooterRoyal/[A-Za-z0-9_/.]+', path) or '..' in path:
                raise ValueError('InputAction must be an explicit /ShooterRoyal asset path')
            value = step.get('value')
            if not isinstance(value, list) or len(value) != 3 or any(type(v) not in (int, float) or not math.isfinite(v) or abs(v) > 1 for v in value):
                raise ValueError('Input pulse requires three finite components in [-1,1]')
        else:
            if step.get('field') not in FIELDS or step.get('op', 'eq') not in ('eq', 'ne', 'ge', 'le'):
                raise ValueError('Unsupported assertion/condition')
            if step['field'] not in ('world_count', 'shader_jobs', 'asset_jobs') and not selector:
                raise ValueError('World field conditions require an explicit selector')
            value = step.get('value')
            if type(value) not in (bool, int, float) or (isinstance(value, float) and not math.isfinite(value)):
                raise ValueError('Conditions require a finite numeric or bool value')
            timeout = step.setdefault('timeout_seconds', 15)
            if type(timeout) not in (int, float) or not 0.1 <= timeout <= 120:
                raise ValueError('Step timeout must be 0.1..120 seconds')
    return result


def select_world(snapshot, selector):
    worlds = snapshot['worlds']
    matched = [w for w in worlds if all(w.get(key) == value for key, value in selector.items())]
    if len(matched) != 1:
        raise RuntimeFault(f'World selector resolved {len(matched)} worlds; expected exactly one')
    return matched[0]


def _compare(actual, expected, op):
    if op == 'eq': return actual == expected
    if op == 'ne': return actual != expected
    if op == 'ge': return actual >= expected
    if op == 'le': return actual <= expected
    raise RuntimeFault('Invalid comparison')


class RuntimeRun:
    """RPC is injected for testing. It must enforce session/lease identity in UE."""
    def __init__(self, spec, rpc, root, *, cancelled=None, clock=time.monotonic, sleep=time.sleep):
        self.spec = normalize(spec)
        self.rpc = rpc
        self.root = Path(root).resolve()
        self.root.mkdir(parents=True, exist_ok=True)
        self.run_id = 'runtime-' + uuid.uuid4().hex
        self.path = self.root / (self.run_id + '.json')
        self.clock, self.sleep = clock, sleep
        self.cancelled = cancelled or threading.Event()
        self.deadline = None
        self.baseline = None
        self.session = None
        self.pie_session = None
        self.owned = False
        self.start_requested = False
        self.unknown = False
        self.report = {'schema': 'unrealbridge.runtime_report.v1', 'run_id': self.run_id,
            'input_hash': digest(self.spec), 'spec': self.spec, 'status': 'queued', 'steps': [], 'jobs': [],
            'save_whitelist': [], 'assets_saved': [], 'recovery': 'continuable_in_current_host',
            'evidence_limits': 'Runtime plumbing and declared assertions only; not gameplay correctness, visual quality or performance benchmarking.'}
        self.persist()

    def persist(self):
        self.report['updated_at'] = time.time()
        temp = self.path.with_suffix('.' + uuid.uuid4().hex + '.tmp')
        temp.write_text(json.dumps(self.report, ensure_ascii=False, indent=2), encoding='utf-8')
        try:
            for attempt in range(10):
                try:
                    os.replace(temp, self.path)
                    break
                except PermissionError:
                    if attempt == 9: raise
                    time.sleep(min(.01 * (attempt + 1), .1))
        finally:
            if temp.exists():
                try: temp.unlink()
                except OSError: pass

    def check(self):
        if self.cancelled.is_set():
            raise RuntimeFault('Cancellation requested; cleanup still pending')
        if self.clock() >= self.deadline:
            raise TimeoutError('Recipe deadline exceeded')

    def call(self, op, args=None, *, cleanup=False):
        if not cleanup: self.check()
        result = self.rpc(op, args or {}, self, cleanup)
        if not isinstance(result, dict) or not result.get('ok'):
            raise RuntimeFault(str((result or {}).get('error', f'{op} returned no verified result')))
        if self.session and result.get('editor_session_id') != self.session:
            self.report['replacement_editor_observation'] = result
            raise RuntimeFault('Editor restarted; old jobs and World handles require reconciliation')
        return result

    def execute(self):
        self.deadline = self.clock() + self.spec['timeout_seconds']
        self.report['status'] = 'running'
        try:
            self.baseline = self.call('snapshot')
            self.session = self.baseline['editor_session_id']
            self.report['baseline'] = self.baseline
            self.report['editor_session_id'] = self.session
            self.persist()
            if self.spec['mode'] == 'owned_pie':
                if self.baseline['pie'] or self.baseline.get('pie_session_id'):
                    raise RuntimeFault('Existing PIE is user-owned; refusing to start or take over')
                if self.baseline['dirty_content'] or self.baseline['dirty_maps']:
                    raise RuntimeFault('Owned PIE requires a clean baseline; unrelated packages will not be saved')
                editor = [w for w in self.baseline['worlds'] if w['world_type'] == 'Editor']
                if len(editor) != 1:
                    raise RuntimeFault('No unambiguous Editor World')
                expected_map = editor[0]['world_path']
                self.start_requested = True
                self.report['start_requested'] = True
                self.persist()  # Intent is durable before any side effect.
                started = self.call('start', {'client_count': self.spec['client_count']})
                self.pie_session = started.get('pie_session_id')
                if not self.pie_session or started.get('lease_owner') != self.run_id:
                    raise RuntimeFault('PIE start ownership could not be proven')
                self.owned = True
                self.report['pie_session_id'] = self.pie_session
                self.persist()
                stable = None
                ready_deadline = min(self.deadline, self.clock() + self.spec['ready_timeout_seconds'])
                while True:
                    self.check()
                    current = self.call('snapshot')
                    if current['pie_session_id'] != self.pie_session:
                        raise RuntimeFault('PIE was replaced during readiness; do not adopt replacement')
                    worlds = [w for w in current['worlds'] if w['world_type'] == 'PIE']
                    ids = tuple(sorted(w['world_handle'] for w in worlds))
                    ready = len(worlds) == self.spec['client_count'] and all(w['has_begun_play'] and re.sub(r'UEDPIE_\d+_', '', w['world_path']) == expected_map for w in worlds)
                    if self.spec['client_count'] == 3:
                        ready = ready and sorted(w['net_mode'] for w in worlds) == ['Client', 'Client', 'ListenServer']
                    if ready and ids == stable:
                        self.report['ready_worlds'] = worlds
                        break
                    stable = ids if ready else None
                    if self.clock() >= ready_deadline:
                        raise TimeoutError('PIE target map/BeginPlay/stable-World readiness timed out')
                    self.sleep(.25)  # Host thread only.
            else:
                self.pie_session = self.baseline.get('pie_session_id')
            for step in self.spec['steps']:
                self.check()
                item = {'id': step['id'], 'input_hash': digest(step), 'status': 'running', 'attempts': 0}
                self.report['steps'].append(item)
                self.persist()
                end = min(self.deadline, self.clock() + step.get('timeout_seconds', 15))
                while True:
                    snapshot = self.call('snapshot')
                    if snapshot.get('pie_session_id') != self.pie_session:
                        raise RuntimeFault('PIE identity changed; stale World selection refused')
                    world = select_world(snapshot, step['world']) if step.get('world') else None
                    item['world_handle'] = world['world_handle'] if world else None
                    item['editor_session_id'] = self.session
                    item['attempts'] += 1
                    if step['type'] == 'input':
                        result = self.call('input', {'world_handle': world['world_handle'], 'input_action_path': step['input_action_path'], 'value': step['value']})
                        item.update(status='succeeded', result=result, side_effect='single_tick_semantic_input')
                        break
                    field = step['field']
                    if field == 'world_count':
                        actual = sum(w['world_type'] == 'PIE' for w in snapshot['worlds'])
                    elif field in ('shader_jobs', 'asset_jobs'):
                        actual = snapshot[field]
                    else:
                        observed = self.call('observe', {'world_handle': world['world_handle']})
                        actual = observed[field]
                        item['observation'] = observed
                    passed = _compare(actual, step['value'], step.get('op', 'eq'))
                    item.update(actual=actual, expected=step['value'], passed=passed)
                    if passed:
                        item['status'] = 'succeeded'
                        break
                    if step['type'] == 'assert':
                        raise AssertionError(f"{step['id']}: {field}={actual!r}, expected {step.get('op','eq')} {step['value']!r}")
                    if self.clock() >= end:
                        raise TimeoutError(f"{step['id']}: condition deadline exceeded")
                    self.persist()
                    self.sleep(.25)
                self.persist()
            self.check()
            self.report['status'] = 'succeeded'
            self.report['recovery'] = 'completed_no_retry'
            return {'success': True, 'run_id': self.run_id, 'report_path': str(self.path), 'assertion_count': len(self.report['steps']), 'rollback_required': False}
        except Exception as exc:
            self.report['status'] = 'cancelled' if self.cancelled.is_set() else 'timed_out' if isinstance(exc, TimeoutError) else 'failed'
            self.report['error'] = str(exc)
            if self.report['steps'] and self.report['steps'][-1]['status'] == 'running':
                self.report['steps'][-1].update(status=self.report['status'], error=str(exc))
            self.report['recovery'] = 'needs_reconciliation' if self.unknown else 'completed_no_retry'
            raise
        finally:
            self.persist()

    def cleanup(self, state=None):
        # Never blind-retry a mutation. The adapter reconciles known outstanding
        # job IDs before a stop request can be considered.
        try:
            current = self.call('reconcile', cleanup=True)
            if self.unknown or current.get('outstanding_jobs'):
                raise RuntimeFault('Unresolved dispatch/job; side effects require reconciliation')
            if self.start_requested:
                if current.get('lease_owner') == self.run_id:
                    # Only an exact BeginPIE session nonce can authorize stop.
                    owned_id = current.get('lease_pie_session_id')
                    if current['pie'] and current['pie_session_id'] == owned_id:
                        self.pie_session = owned_id
                        self.call('stop', cleanup=True)
                        end = self.clock() + 20
                        while True:
                            current = self.call('snapshot', cleanup=True)
                            if not current['pie'] and not current.get('pie_session_id'): break
                            if current.get('pie_session_id') not in (owned_id, '', None):
                                raise RuntimeFault('A replacement PIE appeared during cleanup; it was not stopped')
                            if self.clock() >= end: raise TimeoutError('Owned PIE stop did not finish')
                            self.sleep(.25)
                    elif current['pie']:
                        raise RuntimeFault('PIE replacement is not owned; cleanup refused')
                    self.call('release', cleanup=True)
                elif current['pie']:
                    raise RuntimeFault('PIE ownership unproven; cleanup refused')
            final = self.call('diagnostics', cleanup=True)
            self.report['final'] = final
            if self.baseline and (final['dirty_content'] != self.baseline['dirty_content'] or final['dirty_maps'] != self.baseline['dirty_maps']):
                raise RuntimeFault('Dirty packages changed; no packages saved or rolled back')
            if self.spec['mode'] == 'owned_pie' and self.start_requested and final['pie']:
                raise RuntimeFault('Final PIE remains active')
            result = {'success': True, 'execution_outcome': self.report['status'], 'final': final, 'assets_saved': [], 'side_effects_resolved': True}
            self.report['recovery'] = 'completed_no_retry'
        except Exception as exc:
            result = {'success': False, 'needs_reconciliation': True, 'error': str(exc), 'assets_saved': [], 'side_effects_resolved': False}
            self.report['execution_status'] = self.report['status']
            self.report['status'] = 'needs_reconciliation'
            self.report['recovery'] = 'needs_reconciliation'
        self.report['cleanup'] = result
        self.persist()
        return result


def recovery_view(state, active_in_host):
    value = copy.deepcopy(state)
    if value.get('status') in ACTIVE and not active_in_host:
        value['persisted_status'] = value['status']
        value['status'] = 'needs_reconciliation'
        value['recovery'] = {'state': 'needs_reconciliation', 'reason': 'Persisted runtime executor is absent in this host; no automatic replay, resume or PIE stop',
                             'next': 'Read retained report, query the exact Editor session and outstanding job IDs, then reconcile ownership.'}
    elif value.get('status') not in ACTIVE:
        value['recovery'] = {'state': 'needs_reconciliation' if value['status'] == 'needs_reconciliation' else 'completed_no_retry'}
    else:
        value['recovery'] = {'state': 'continuable_in_current_host'}
    return value
