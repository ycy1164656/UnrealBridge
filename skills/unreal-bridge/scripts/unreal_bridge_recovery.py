"""Opt-in, process-external Editor recovery with exact identity and bounded relaunch.

No build, sync, force kill, desktop input or automatic replay is performed here.
The companion reads existing production/Job checkpoints and stops on uncertainty.
"""
from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

from unreal_bridge_production import atomic_json, identifier
from unreal_bridge_sessions import process_identity, same_process, safe_path


class ProjectRecoveryLock:
    """OS lock is released on process death. Stale metadata never authorizes takeover."""
    def __init__(self, path):
        self.path = Path(path)
        self.stream = None

    def __enter__(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.stream = self.path.open('a+b')
        try:
            self.stream.seek(0)
            if self.stream.read(1) == b'':
                self.stream.write(b'0')
                self.stream.flush()
            self.stream.seek(0)
            if os.name == 'nt':
                import msvcrt
                msvcrt.locking(self.stream.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(self.stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            self.stream.close()
            self.stream = None
            raise ValueError('Another recovery owner holds this project lock')
        return self

    def __exit__(self, *args):
        if self.stream:
            self.stream.seek(0)
            if os.name == 'nt':
                import msvcrt
                msvcrt.locking(self.stream.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                import fcntl
                fcntl.flock(self.stream.fileno(), fcntl.LOCK_UN)
            self.stream.close()


def classify_exit(expected, current, *, exit_code=None, process_signaled=False, cancelled=False, normal_exit=False, debugger=False):
    if cancelled or normal_exit:
        return 'stopped_normal'
    if current and not same_process(expected, current):
        return 'identity_conflict'
    # GetExitCodeProcess can expose a final code before kernel termination has
    # completed. A still-unsignaled process can retain DLL/file locks; never
    # launch another Editor until the held original handle is signaled.
    if type(exit_code) is int and exit_code != 259:
        if not process_signaled:
            return 'termination_pending'
        return 'stopped_normal' if exit_code == 0 else 'abnormal_exit_confirmed'
    if current:
        return 'debugger_or_busy' if debugger else 'alive'
    return 'needs_attention_unknown_exit'


class ExitHandle:
    def __init__(self, identity):
        if os.name != 'nt' or not same_process(identity, process_identity(identity['pid'])):
            raise ValueError('Live exact Windows Editor identity required')
        self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        self.kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        self.kernel.OpenProcess.restype = wintypes.HANDLE
        self.kernel.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
        self.kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        self.kernel.WaitForSingleObject.restype = wintypes.DWORD
        self.kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        self.handle = self.kernel.OpenProcess(0x1000 | 0x100000, False, identity['pid'])
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())
        if not same_process(identity, process_identity(identity['pid'])):
            self.close()
            raise ValueError('Process changed while acquiring observation handle')

    def exit_code(self):
        value = wintypes.DWORD()
        if not self.kernel.GetExitCodeProcess(self.handle, ctypes.byref(value)):
            raise ctypes.WinError(ctypes.get_last_error())
        return value.value

    def is_signaled(self):
        result = self.kernel.WaitForSingleObject(self.handle, 0)
        if result == 0:
            return True
        if result == 258:
            return False
        raise ctypes.WinError(ctypes.get_last_error())

    def close(self):
        if self.handle:
            self.kernel.CloseHandle(self.handle)
            self.handle = None


def normalize_policy(policy):
    required = {'work_order_id', 'project', 'exe', 'map', 'editor_identity', 'editor_session_id',
                'plugin_version', 'registry_hash', 'manifest_hash', 'allow_relaunch', 'max_restarts', 'monitor_seconds'}
    if not isinstance(policy, dict) or set(policy) != required:
        raise ValueError('Exact recovery policy fields required')
    value = dict(policy)
    identifier(value['work_order_id'])
    project, exe = safe_path(value['project']), safe_path(value['exe'])
    if project.suffix.lower() != '.uproject' or exe.name.lower() != 'unrealeditor.exe':
        raise ValueError('Exact .uproject and UnrealEditor executable required')
    if not re.fullmatch(r'/[A-Za-z0-9_]+/[A-Za-z0-9_/]+', value['map']):
        raise ValueError('Map package only; command-line options and scripts are forbidden')
    if type(value['allow_relaunch']) is not bool or type(value['max_restarts']) is not int or not 0 <= value['max_restarts'] <= 1:
        raise ValueError('Explicit relaunch permission and at most one restart required')
    if type(value['monitor_seconds']) is not int or not 5 <= value['monitor_seconds'] <= 1800:
        raise ValueError('Monitoring lease must be 5..1800 seconds')
    if not isinstance(value['editor_identity'], dict) or os.path.normcase(value['editor_identity'].get('exe', '')) != os.path.normcase(str(exe)):
        raise ValueError('Executable identity mismatch')
    for name in ('editor_session_id', 'registry_hash', 'manifest_hash', 'plugin_version'):
        if not isinstance(value[name], str) or not value[name] or len(value[name]) > 128:
            raise ValueError('Version/session identity is required')
    value['project'], value['exe'] = str(project), str(exe)
    return value


def unresolved_dispatch(project, work_order_id):
    """Reuse existing host/native work-order and Job records; do not infer success after host death."""
    root = Path(project).parent / 'Saved/UnrealBridge'
    files = [root / 'Artifacts/production' / (work_order_id + '.json'),
             root / 'ProductionOperations' / (work_order_id + '.json')]
    unknown = []
    for path in files:
        if not path.exists():
            continue
        state = json.loads(path.read_text(encoding='utf-8'))
        for key in ('steps', 'receipts'):
            for step_id, receipt in state.get(key, {}).items():
                if receipt.get('phase') in {'dispatched', 'outcome_unknown', 'intent'}:
                    unknown.append({'step_id': step_id, 'phase': receipt['phase'], 'job_id': receipt.get('job_id')})
    return unknown


def redact(text):
    text = re.sub(r'(?i)(token|api[_-]?key|authorization|password|secret)([\s=:]+)[^\s,;]+', r'\1\2[redacted]', text)
    return re.sub(r'https?://[^\s]+[?][^\s]+', '[signed-url-redacted]', text)


def crash_evidence(project, expected, exit_code, state_root):
    """Bounded local diagnostic metadata. Uncorrelated latest crash files are never assumed owned."""
    saved = Path(project).parent / 'Saved'
    records = []
    crash_root = saved / 'Crashes'
    if crash_root.is_dir():
        for folder in list(crash_root.iterdir())[:64]:
            context = folder / 'CrashContext.runtime-xml'
            if not context.is_file() or context.stat().st_size > 2 * 1024 * 1024:
                continue
            import xml.etree.ElementTree as ET
            try:
                tree = ET.fromstring(context.read_bytes())
                pid = tree.findtext('.//ProcessId')
                created_unix = expected['creation_time'] / 10_000_000 - 11644473600
                if pid != str(expected['pid']) or context.stat().st_mtime < created_unix:
                    continue
                error = redact(tree.findtext('.//ErrorMessage') or '')[:4096]
                stack = redact(tree.findtext('.//PCallStack') or tree.findtext('.//CallStack') or '')[:8192]
                records.append({'context': str(context), 'error': error, 'callstack': stack,
                                'sha256': hashlib.sha256(context.read_bytes()).hexdigest()})
            except (ET.ParseError, OSError):
                continue
    signature = hashlib.sha256(json.dumps({'exit_code': exit_code, 'errors': [r['error'] for r in records],
                                           'stacks': [r['callstack'] for r in records]}, sort_keys=True).encode()).hexdigest()
    value = {'process': expected, 'exit_code': exit_code, 'signature': signature, 'crash_contexts': records,
             'root_cause': 'not_determined', 'symbols_downloaded': False, 'uploaded': False}
    atomic_json(Path(state_root) / ('exit-' + signature[:16] + '.json'), value)
    return value


class RecoverySupervisor:
    def __init__(self, policy, *, probe=process_identity, launcher=None, readiness=None):
        self.policy = normalize_policy(policy)
        self.root = Path(self.policy['project']).parent / 'Saved/UnrealBridge/Artifacts/recovery'
        self.path = self.root / (self.policy['work_order_id'] + '.json')
        self.control = self.root / (self.policy['work_order_id'] + '-control.json')
        self.probe, self.launcher = probe, launcher or self._launch
        self.readiness = readiness or self._readiness

    def initialize(self):
        if self.path.exists():
            old = json.loads(self.path.read_text(encoding='utf-8'))
            if old['policy'] != self.policy:
                raise ValueError('Recovery policy changed; old ledger must be reconciled')
            return old
        if not same_process(self.policy['editor_identity'], self.probe(self.policy['editor_identity']['pid'])):
            raise ValueError('Recovery ownership requires a live exact Editor')
        state = {'schema': 'unrealbridge.recovery.v1', 'policy': self.policy, 'status': 'registered_not_monitoring',
                 'identity': self.policy['editor_identity'], 'restarts': 0, 'signatures': [], 'unknown_effects': [],
                 'old_handles_valid': False, 'started_utc': time.time(), 'checkpoint_action': 'reconcile_only'}
        atomic_json(self.path, state)
        return state

    def _launch(self):
        arguments = [self.policy['exe'], self.policy['project'], self.policy['map'],
                     '-RenderOffScreen', '-NoSplash', '-unattended', '-Windowed', '-ResX=1280', '-ResY=720']
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        process = subprocess.Popen(arguments, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.DEVNULL, startupinfo=startup, shell=False)
        identity = self.probe(process.pid)
        if not identity:
            raise RuntimeError('Launched process identity unavailable; do not launch again')
        return identity

    def _readiness(self, expected_identity):
        bridge = Path(__file__).with_name('bridge.py')
        result = subprocess.run([sys.executable, str(bridge), '--project=' + self.policy['project'], '--json', 'ping'],
                                capture_output=True, text=True, encoding='utf-8', timeout=10)
        try:
            ping = json.loads(result.stdout)
        except ValueError:
            return {'ready': False}
        for field in ('plugin_version', 'registry_hash', 'manifest_hash'):
            if ping.get(field) != self.policy[field]:
                return {'ready': False, 'identity_mismatch': field}
        if not ping.get('ready'):
            return {'ready': False}
        # A different Editor for the same project is not the child we launched.
        code = ('import json,os,unreal\n'
                's=json.loads(unreal.UnrealBridgeSandboxLibrary.get_sandbox_status())\n'
                "print(json.dumps({'pid':os.getpid(),'session':s['editor_session_id'],'project':s['project_identity']}))")
        identity_result = subprocess.run([sys.executable, str(bridge), '--project=' + self.policy['project'], '--json', 'exec', '--stdin'],
                                        input=code, capture_output=True, text=True, encoding='utf-8', timeout=15)
        try:
            response=json.loads(identity_result.stdout)
            identity=json.loads(response['output'].splitlines()[-1])
        except (ValueError, KeyError, IndexError):
            return {'ready':False}
        if identity.get('pid') != expected_identity['pid'] or identity.get('session') == self.policy['editor_session_id']:
            return {'ready':False, 'identity_mismatch':'restarted_editor_pid_or_session'}
        if os.path.normcase(os.path.abspath(identity.get('project',''))) != os.path.normcase(self.policy['project']):
            return {'ready':False, 'identity_mismatch':'project'}
        # Readiness still requires a fresh session/World/sandbox/Dirty query before writes.
        return {'ready': True, 'ping': ping, 'fresh_session':identity['session']}

    def tick(self, *, exit_code=None, process_signaled=False, debugger=False):
        state = json.loads(self.path.read_text(encoding='utf-8'))
        control = json.loads(self.control.read_text(encoding='utf-8')) if self.control.exists() else {}
        if control.get('operation') in {'stop', 'pause'}:
            state['status'] = 'stopped_by_user' if control['operation'] == 'stop' else 'paused'
            atomic_json(self.path, state)
            return state
        if state['status'] in {'stopped_normal', 'stopped_by_user', 'paused', 'circuit_open', 'needs_reconciliation', 'identity_conflict', 'needs_attention_unknown_exit', 'recovered_requires_fresh_context'}:
            return state
        if state['status'] == 'launch_intent':
            state.update(status='needs_reconciliation', reason='launch_dispatch_outcome_unknown')
            atomic_json(self.path, state)
            return state
        if state['status'] == 'waiting_ready':
            if not same_process(state['identity'], self.probe(state['identity']['pid'])):
                state.update(status='circuit_open', reason='restarted_process_exited_or_replaced')
                atomic_json(self.path, state)
                return state
            ready = self.readiness(state['identity'])
            if ready.get('identity_mismatch'):
                state.update(status='needs_reconciliation', reason='loaded_identity_mismatch')
            elif ready.get('ready'):
                state.update(status='recovered_requires_fresh_context', old_handles_valid=False,
                             fresh_session=ready.get('fresh_session'),
                             checkpoint_action='refresh_catalog_world_sandbox_and_reconcile_before_writes')
            atomic_json(self.path, state)
            return state
        current = self.probe(state['identity']['pid'])
        status = classify_exit(state['identity'], current, exit_code=exit_code, debugger=debugger,
                               process_signaled=process_signaled,
                               normal_exit=bool(control.get('normal_exit')))
        state['status'] = status
        if status in {'alive','abnormal_exit_confirmed'}:
            state.pop('reason',None)
        if status != 'abnormal_exit_confirmed':
            atomic_json(self.path, state)
            return state
        evidence = crash_evidence(self.policy['project'], state['identity'], exit_code, self.root)
        signature = evidence['signature']
        unknown = unresolved_dispatch(self.policy['project'], self.policy['work_order_id'])
        state['unknown_effects'] = unknown
        if unknown:
            state.update(status='needs_reconciliation', reason='dispatched_side_effects_unresolved')
        elif not self.policy['allow_relaunch']:
            state.update(status='needs_reconciliation', reason='relaunch_not_authorized')
        elif state['restarts'] >= self.policy['max_restarts'] or signature in state['signatures']:
            state.update(status='circuit_open', reason='restart_budget_or_repeated_signature')
        else:
            state['signatures'].append(signature)
            state['restarts'] += 1
            state.update(status='launch_intent', old_handles_valid=False)
            atomic_json(self.path, state)
            try:
                state['identity'] = self.launcher()
                state['status'] = 'waiting_ready'
            except Exception as error:
                state.update(status='needs_reconciliation', reason='launch_outcome_unknown', error=redact(str(error))[:1024])
        atomic_json(self.path, state)
        return state

    def watch(self):
        with ProjectRecoveryLock(self.root / 'project-recovery.lock'):
            state = self.initialize()
            handle = ExitHandle(state['identity'])
            deadline = time.monotonic() + self.policy['monitor_seconds']
            try:
                while time.monotonic() < deadline:
                    signaled = handle.is_signaled()
                    state = self.tick(exit_code=handle.exit_code(), process_signaled=signaled)
                    if state['status'] not in {'alive', 'debugger_or_busy', 'waiting_ready', 'abnormal_exit_confirmed', 'termination_pending'}:
                        return state
                    time.sleep(1)  # Companion process only; never the Editor GameThread.
                state.update(status='lease_expired', reason='monitoring_budget_exhausted')
                atomic_json(self.path, state)
                return state
            finally:
                handle.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--policy', required=True, type=Path)
    parser.add_argument('--watch', action='store_true', help='Explicitly enable the bounded companion; registration alone does not monitor')
    args = parser.parse_args()
    supervisor = RecoverySupervisor(json.loads(safe_path(args.policy).read_text(encoding='utf-8')))
    result = supervisor.watch() if args.watch else supervisor.initialize()
    print(json.dumps({'status': result['status'], 'restarts': result['restarts'], 'report': str(supervisor.path)}))
