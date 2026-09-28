"""Durable narrow import contracts; never infer absence of effects from timeout."""
from __future__ import annotations
import json
import hashlib
import re
import threading
from pathlib import Path
from unreal_bridge_production import atomic_json, identifier, sha

_IMPORT_LOCK = threading.RLock()


class ImportOrder:
    def __init__(self, root, identity):
        identifier(identity)
        self.path = Path(root) / 'external-imports' / (identity + '.json')
        self.identity = identity

    def get(self):
        state = json.loads(self.path.read_text(encoding='utf-8'))
        if sha(state['request']) != state['request_hash']:
            raise ValueError('Persisted import request was modified')
        if state.get('contract') and sha(state['contract']) != state.get('host_contract_hash'):
            raise ValueError('Persisted native import contract was modified')
        return state

    def freeze(self, request):
        with _IMPORT_LOCK:
            if request.get('schema') != 'unrealbridge.external_import.v1' or request.get('request_id') != self.identity:
                raise ValueError('Exact external_import.v1 request identity required')
            if self.path.exists():
                state = self.get()
                if state['request_hash'] != sha(request):
                    raise ValueError('Import identity already binds different source/options/targets')
                return state
            state = {'request':request, 'request_hash':sha(request), 'phase':'frozen', 'side_effect_state':'none'}
            atomic_json(self.path,state)
            return state

    def update(self, **changes):
        with _IMPORT_LOCK:
            state=self.get()
            state.update(changes)
            if 'contract' in changes:
                state['host_contract_hash']=sha(changes['contract'])
            atomic_json(self.path,state)
            return state

    def dispatch_intent(self):
        with _IMPORT_LOCK:
            state=self.get()
            if not state.get('contract') or state['phase'] != 'prepared':
                raise ValueError('Import has no prepared unconsumed contract; reconcile existing intent/Job')
            return self.update(phase='dispatch_intent', side_effect_state='unknown')

    def observe_saved(self, actual):
        """Called only with a completed native readback, never caller evidence."""
        state = self.get()
        proof = {'status': 'imported_not_saved'}
        if not actual.get('ok') or actual.get('save_blocked') or actual.get('dirty'):
            return self.update(phase=proof['status'], saved_observation=proof)
        if not actual.get('saved_in_current_view'):
            return self.update(phase=proof['status'], saved_observation=proof)
        original_session, original_root, lease_id = state['contract']['view'].split('|')
        if actual.get('sandbox_active'):
            if actual.get('current_view') != state['contract']['view']:
                raise ValueError('Saved import is observed from a different Sandbox view')
            proof['status'] = 'saved_in_sandbox'
        else:
            project = Path(actual['project_identity']).resolve(strict=True)
            root = Path(original_root).resolve()
            if not root.is_relative_to(project.parent / 'intermediate' / 'sandboxes'):
                raise ValueError('Original import Sandbox is outside this project')
            if not re.fullmatch(r'UB_[A-Za-z0-9_-]+', root.name, re.I):
                raise ValueError('Invalid original Sandbox name')
            lease = json.loads((project.parent / 'Saved/UnrealBridge/SandboxLeases' / (root.name+'.json')).read_text(encoding='utf-8'))
            if lease.get('lease_id') != lease_id or lease.get('project','').casefold() != str(project).replace('\\','/').casefold():
                raise ValueError('Persist receipt identity mismatch')
            file = Path(actual['package_filename']).resolve(strict=True)
            if not file.is_relative_to(project.parent) or file.suffix.lower() != '.uasset' or file.stat().st_size > 64*1024*1024:
                raise ValueError('Observed import package path or size is outside proof limits')
            key = str(file).replace('\\','/').lower()
            sealed = lease.get('sealed',{}).get(key)
            receipt = next((r for r in lease.get('persist_results',[]) if r.get('path') == key),{})
            raw = file.read_bytes()
            if not sealed or not receipt.get('confirmed') or receipt.get('main_sha1') != sealed or hashlib.sha1(raw).hexdigest() != sealed or hashlib.sha256(raw).hexdigest() != actual['package_sha256']:
                raise ValueError('Saved main package differs from confirmed import Persist receipt')
            proof.update(status='cold_loaded' if actual['editor_session_id'] != original_session else 'persisted',
                         package_filename=key, package_sha256=actual['package_sha256'], lease_id=lease_id,
                         original_session=original_session, observed_session=actual['editor_session_id'])
        return self.update(phase=proof['status'], saved_observation=proof, side_effect_state='complete')
