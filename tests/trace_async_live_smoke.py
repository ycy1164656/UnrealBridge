"""Real UE 5.8 trace analysis acceptance; writes reports, never assets."""
from __future__ import annotations

import argparse
import json
import math
import threading
import time
import uuid
from pathlib import Path

from network_multiclient_v3_live_smoke import _load_server, _native, _exec_json, _editor_state


def normalize(value):
    if isinstance(value, dict):
        output = {}
        for key, item in value.items():
            if isinstance(item, bool) and key.startswith('b') and key[1:2].isupper():
                key = key[1:]
            output[''.join(c.lower() for c in key if c.isalnum())] = normalize(item)
        return output
    if isinstance(value, list):
        return [normalize(item) for item in value]
    return value


def compare(before, after, path='summary'):
    assert type(before) is type(after) or isinstance(before, (int, float)) and isinstance(after, (int, float)), (path, type(before), type(after))
    if isinstance(before, dict):
        assert set(before) == set(after), (path, set(before) ^ set(after))
        for key in before:
            compare(before[key], after[key], f'{path}.{key}')
    elif isinstance(before, list):
        assert len(before) == len(after), (path, len(before), len(after))
        for index, (a, b) in enumerate(zip(before, after)):
            compare(a, b, f'{path}[{index}]')
    elif isinstance(before, (int, float)) and not isinstance(before, bool):
        assert math.isclose(before, after, rel_tol=2e-6, abs_tol=1e-6), (path, before, after)
    else:
        assert before == after, (path, before, after)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--project', default='C:/dev/ShooterRoyal_5_8_DirectUpgrade/ShooterRoyal.uproject')
    parser.add_argument('--report', required=True)
    args = parser.parse_args()
    report_path = Path(args.report)
    assert not report_path.exists()
    baseline = json.loads(Path('.tmp/ub-reliability/trace-baseline.json').read_text(encoding='utf-8'))
    small, medium = [row['start']['path'] for row in baseline['samples']]
    server = _load_server()
    assert server.bridge_ping(project=args.project)['ready']
    assert server.bridge_health(project=args.project)['success']
    before = _editor_state(server, args.project, None)
    assert not before['pie'] and not before['dirty_content'] and not before['dirty_maps'], before
    report = {'before': before, 'sample_bytes': [row['bytes'] for row in baseline['samples']], 'checks': []}
    owned = []

    def native(function, kwargs=None):
        return json.loads(_native(server, args.project, 'Perf', function, kwargs or {}, None))

    def start(path, kind='performance', **kwargs):
        result = native('start_trace_analysis', {'utrace_path': path, 'summary_kind': kind, **kwargs})
        if result.get('analysis_id'):
            owned.append(result['analysis_id'])
        return result

    def finish(analysis_id):
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            state = native('get_trace_analysis_status', {'analysis_id': analysis_id})
            assert state['ok'], state
            if state['terminal']:
                assert state['resources_released'], state
                return native('get_trace_analysis_result', {'analysis_id': analysis_id})
            time.sleep(0.05)
        raise TimeoutError(analysis_id)

    def probe():
        return server._send_command({'id': str(uuid.uuid4()), 'command': 'gamethread_ping', 'timeout': 1.0}, project=args.project, timeout=4.0)

    def checked(label, data=None):
        report['checks'].append({'name': label, 'data': data})
        print(json.dumps({'passed': label}), flush=True)

    try:
        probes_before = [probe() for _ in range(5)]
        assert all(row['success'] for row in probes_before), probes_before
        report['baseline_gt_probes'] = probes_before
        checked('missing_size_kind_and_header_guards', {
            'missing': start(str(Path('.tmp/ub-reliability/missing.utrace').resolve())),
            'size': start(small, max_file_size_mb=1),
            'kind': start(small, 'invalid'),
        })
        errors = report['checks'][-1]['data']
        assert [row['error_code'] for row in errors.values()] == ['trace_missing_or_empty', 'trace_size_limit', 'invalid_summary_kind'], errors
        for suffix, contents in [('bad-magic', b'invalid trace'), ('incomplete', b'2CRT\x28\x00\x02\x00')]:
            path = report_path.with_suffix(f'.{suffix}.utrace').resolve()
            with path.open('xb') as stream:
                stream.write(contents)
            result = start(path.as_posix())
            if result.get('ok'):
                result = finish(result['analysis_id'])
            assert not result['ok'], result
            checked(suffix, result)

        concurrent = _exec_json(server, args.project,
            'import json, unreal\n'
            f'_a = json.loads(unreal.UnrealBridgePerfLibrary.start_trace_analysis({small!r}))\n'
            f'_dup = json.loads(unreal.UnrealBridgePerfLibrary.start_trace_analysis({small!r}))\n'
            f'_b = json.loads(unreal.UnrealBridgePerfLibrary.start_trace_analysis({medium!r}))\n'
            f'_cap = json.loads(unreal.UnrealBridgePerfLibrary.start_trace_analysis({small!r}))\n'
            "_c = [json.loads(unreal.UnrealBridgePerfLibrary.cancel_trace_analysis(x['analysis_id'])) for x in (_a, _b)]\n"
            "print(json.dumps({'a':_a, 'b':_b, 'duplicate':_dup, 'capacity':_cap, 'cancel':_c}))", None)
        owned.extend([concurrent['a']['analysis_id'], concurrent['b']['analysis_id']])
        assert concurrent['duplicate']['error_code'] == 'trace_already_analyzing', concurrent
        assert concurrent['capacity']['error_code'] == 'analysis_capacity_reached', concurrent
        for request in concurrent['cancel']:
            assert request['state'] == 'cancelling' and not request['terminal'], request
            cancelled = finish(request['analysis_id'])
            assert cancelled['state'] == 'cancelled' and 'summary' not in cancelled, cancelled
        checked('capacity_duplicate_cancel_and_release_semantics', concurrent)

        stop_probe = threading.Event()
        during_probes = []
        def probe_loop():
            while not stop_probe.is_set():
                during_probes.append(probe())
                stop_probe.wait(0.025)
        thread = threading.Thread(target=probe_loop)
        thread.start()
        try:
            started = start(small)
            assert started['ok'], started
            result = finish(started['analysis_id'])
        finally:
            stop_probe.set()
            thread.join(5)
        assert result['state'] == 'succeeded', result
        assert all(row['success'] for row in during_probes), during_probes
        report['submission_to_terminal_gt_probes'] = during_probes
        report['small_result'] = result
        compare(normalize(baseline['samples'][0]['summary']), normalize(result['summary']))
        assert native('get_trace_analysis_result', {'analysis_id': started['analysis_id']}) == result
        checked('small_summary_matches_all_baseline_fields_and_repeat_is_stable')

        for kind in ('performance', 'alloc', 'net', 'cook'):
            started = start(medium, kind)
            assert started['ok'], started
            result = finish(started['analysis_id'])
            assert result['state'] == 'succeeded', result
            report[kind + '_result'] = result
        checked('four_summary_kinds_complete')

        for analysis_id in owned:
            native('get_trace_analysis_result', {'analysis_id': analysis_id, 'release': True})
            state = native('get_trace_analysis_status', {'analysis_id': analysis_id})
            assert state['error_code'] == 'unknown_or_expired_analysis', state
        checked('all_results_explicitly_released')
        report['passed'] = True
    except Exception as exc:
        report['passed'] = False
        report['failure'] = repr(exc)
        raise
    finally:
        cleanup = []
        for analysis_id in owned:
            state = native('get_trace_analysis_status', {'analysis_id': analysis_id})
            if state.get('ok') and not state['terminal']:
                native('cancel_trace_analysis', {'analysis_id': analysis_id})
                cleanup.append(finish(analysis_id))
        report['cleanup'] = cleanup
        report['after'] = _editor_state(server, args.project, None)
        report['trace_capture_active'] = _native(server, args.project, 'Perf', 'get_trace_state', {}, None)['active']
        with report_path.open('x', encoding='utf-8') as stream:
            json.dump(report, stream, ensure_ascii=False, indent=2)
        assert report['before'] == report['after'] and not report['trace_capture_active'], report['after']
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
