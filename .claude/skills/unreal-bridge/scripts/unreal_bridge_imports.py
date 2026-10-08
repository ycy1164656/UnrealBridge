"""Durable narrow import contracts; never infer absence of effects from timeout."""
from __future__ import annotations
import json
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
