"""Old blocking operations must be rejected without an Editor round trip."""
import unittest
from unittest.mock import patch
from network_multiclient_v3_live_smoke import _load_server

server = _load_server()


class TraceAsyncHostTests(unittest.TestCase):
    def test_sync_calls_never_dispatch(self):
        with patch.object(server, '_execute_code') as execute:
            for library in ('Perf', 'perf', 'UnrealBridgePerfLibrary'):
                for function in ('parse_trace_to_summary', 'parse_alloc_trace_to_summary',
                                 'parse_net_trace_to_summary', 'parse_cook_trace_to_summary'):
                    result = server.bridge_call(library, function, {'utrace_path': 'C:/trace.utrace'})
                    self.assertFalse(result['success'])
                    self.assertEqual(result['error_code'], 'synchronous_analysis_disabled')
        execute.assert_not_called()

    def test_compatibility_schema_remains_discoverable(self):
        result = server.bridge_describe('Perf', 'parse_trace_to_summary')
        self.assertTrue(result['success'])


if __name__ == '__main__':
    unittest.main()
