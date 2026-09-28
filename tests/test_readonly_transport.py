"""Exercise the real host transport through queued Jobs, not a FakeEditor array."""
import hashlib
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'skills/unreal-bridge/scripts'))
import unreal_bridge_mcp_server as host
import unreal_bridge_audit as audit


class ReadonlyTransportTests(unittest.TestCase):
    def terminal(self, payload):
        return {'success': True, 'job_id': 'job-test', 'terminal': True,
                'job_state': 'succeeded', 'job_result': {'success': True, 'output': json.dumps(payload)}}

    def run_call(self, snapshots, timeout=5, artifact=None):
        submitted = {'success': True, 'job_id': 'job-test', 'job_state': 'queued', 'provider': 'EpicToolsetRegistry'}
        with patch.object(host, 'bridge_submit_official_toolset_job', return_value=submitted), \
             patch.object(host, 'bridge_wait_job', side_effect=snapshots), \
             patch.object(host, 'bridge_read_artifact', return_value=artifact):
            return host._official_transport('127.0.0.1:1', 'test', None, timeout)(
                toolset='EditorAssetToolset', tool='find_assets', arguments={'path': '/Game/Test'})

    def test_queued_and_running_are_waited(self):
        running = {'success': True, 'job_id': 'job-test', 'job_state': 'running', 'terminal': False}
        result = self.run_call([running, self.terminal({'ok': True, 'result': ['/Game/Test/A']})])
        self.assertTrue(result['result_complete'])
        self.assertEqual(result['result'], ['/Game/Test/A'])
        self.assertEqual(result['job_id'], 'job-test')

    def test_terminal_failure_never_becomes_empty(self):
        for state in ('failed', 'cancelled', 'expired', 'aborted'):
            result = self.run_call([{'terminal': True, 'job_state': state, 'error': 'native problem'}])
            self.assertFalse(result['success'])
            self.assertEqual(result['job_state'], state)

    def test_deadline_retains_job_and_no_side_effects(self):
        result = self.run_call([], timeout=0)
        self.assertEqual(result['error_code'], 'client_timeout')
        self.assertEqual(result['job_id'], 'job-test')
        self.assertEqual(result['side_effect_state'], 'none')

    def test_native_failure(self):
        result = self.run_call([self.terminal({'ok': False, 'error': 'schema mismatch'})])
        self.assertEqual(result['error_code'], 'native_failed')

    def test_truncated_without_full_artifact_is_not_clean(self):
        result = self.run_call([self.terminal({'result': [], 'truncated': True})])
        self.assertFalse(result['result_complete'])
        with self.assertRaises(audit.AuditError):
            audit.collect_assets(lambda **kwargs: result, ['/Game/Test'])

    def test_artifact_replaces_truncated_inline_page(self):
        raw = json.dumps(['/Game/Test/A', '/Game/Test/B']).encode()
        digest = hashlib.sha256(raw).hexdigest()
        result = self.run_call([self.terminal({'result': [], 'truncated': True,
            'artifact': {'artifact_id': digest + '.json', 'sha256': digest}})],
            artifact={'offset': 0, 'next_offset': None, 'encoding': 'utf-8',
                      'size_bytes': len(raw), 'content': raw.decode()})
        self.assertEqual(len(result['result']), 2)
        self.assertTrue(result['result_complete'])

    def test_malformed_and_missing_arrays_fail_closed(self):
        result = self.run_call([{'terminal': True, 'job_state': 'succeeded', 'output': 'not JSON'}])
        self.assertEqual(result['error_code'], 'malformed_native_result')
        for payload in (None, {'job_state': 'queued'}, {'count': 0}):
            with self.assertRaises(audit.AuditError):
                audit.collect_assets(lambda **kwargs: payload, ['/Game/Test'])


if __name__ == '__main__':
    unittest.main()
