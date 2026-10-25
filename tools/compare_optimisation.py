#!/usr/bin/env python3
"""Check matched animation runs. A pass covers only the recorded steady-state cases."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import statistics

from benchmark_saved_preset import percentile

DIAGNOSTIC = {'control-stars', 'saved-stars'}  # stochastic population
CONTEXT = ('board', 'rig', 'pin', 'pixels', 'brightness_limit', 'output_type',
           'network_regime', 'warmup_seconds', 'seconds_requested', 'poll_hz')


def finite(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def identity(document):
    context = {key: document[key] for key in CONTEXT}
    context['temperature_monitor'] = document.get('temperature_monitor')
    context['packages'] = {name: row['bytecode_sha256'] for name, row in document['packages'].items()}
    context['cases'] = sorted(document['selected_cases'])
    return context


def load_runs(paths, seen):
    documents, groups, reference = [], {}, None
    firmware = set()
    for path in paths:
        raw = path.read_bytes()
        doc = json.loads(raw)
        if doc.get('schema') != 'lightvm.optimisation-run.v1':
            raise ValueError(f'{path}: use new protocol runs; historical summaries are reference evidence')
        if doc['run_id'] in seen:
            raise ValueError(f'{path}: duplicate run (including across baseline/candidate)')
        seen.add(doc['run_id'])
        if not doc.get('measurement_complete') or not doc.get('temporary_cleanup_complete'):
            raise ValueError(f'{path}: incomplete measurement or cleanup')
        current = identity(doc)
        if reference is not None and current != reference:
            raise ValueError(f'{path}: incompatible context within one firmware group')
        reference = current
        if not doc['selected_cases'] or len(set(doc['selected_cases'])) != len(doc['selected_cases']):
            raise ValueError(f'{path}: empty or duplicate case selection')
        expected = {(case, trial) for case in doc['selected_cases']
                    for trial in range(1, doc['samples_requested'] + 1)}
        actual = [(row['case'], row['trial']) for row in doc['samples']]
        if set(actual) != expected or len(actual) != len(expected):
            raise ValueError(f'{path}: missing or duplicate trials')
        firmware.add(doc['firmware_sha256'])
        for row in doc['samples']:
            if not finite(row['seconds']) or row['seconds'] <= 0 or not row['layers']:
                raise ValueError(f'{path}: invalid duration or empty layers')
            if row['description_before']['boot_id'] != row['description_after']['boot_id']:
                raise ValueError(f'{path}: reboot during sample')
            row['_instrumented'] = doc['ingress_instrumented']
            groups.setdefault(row['case'], []).append(row)
        documents.append({'file': str(path), 'sha256': hashlib.sha256(raw).hexdigest(), 'run_id': doc['run_id']})
    if len(firmware) != 1:
        raise ValueError('each group must contain exactly one frozen firmware image')
    for case, rows in groups.items():
        for row in rows:
            if set(row['layers']) != set(rows[0]['layers']) or row['settings'] != rows[0]['settings']:
                raise ValueError(f'{case}: layer set or settings changed within group')
            for layer in row['layers'].values():
                if layer['before']['generation'] != layer['after']['generation']:
                    raise ValueError(f'{case}: generation changed during sample')
                if not finite(layer['updates_per_second']) or layer['updates_per_second'] < 0:
                    raise ValueError(f'{case}: invalid update rate')
                if layer['mean_render_us'] is not None and (not finite(layer['mean_render_us']) or layer['mean_render_us'] <= 0):
                    raise ValueError(f'{case}: invalid render time')
    return reference, groups, documents, next(iter(firmware))


def spread(values):
    return {'median': statistics.median(values), 'min': min(values), 'max': max(values), 'windows': values}


def service(rows):
    latencies = [value for row in rows for value in row['status_latency_ms']['samples']]
    if any(not finite(v) or v < 0 for v in latencies):
        raise ValueError('invalid latency sample')
    gaps = []
    memory_known, memory_flat = True, True
    for row in rows:
        observations = row['foreground_gap_samples']
        if row['_instrumented'] and len(observations) >= 2 and all(o.get('ingress_over_10ms') is not None for o in observations):
            counts = [o['ingress_over_10ms'] for o in observations]
            if any(b < a for a, b in zip(counts, counts[1:])):
                raise ValueError('ingress counter reset inside sample')
            gaps.append(counts[-1] - counts[0])
        heaps = row['heap_samples']
        if len(heaps) < 2 or any('heap' not in h for h in heaps):
            memory_known = False
        else:
            memory_flat &= len({h['heap'] for h in heaps}) == 1
    return {'status_samples': len(latencies),
            'usb_p95_ms': percentile(latencies, .95) if latencies else None,
            'usb_p99_ms': percentile(latencies, .99) if latencies else None,
            'usb_max_ms': max(latencies) if latencies else None,
            'ingress_over_10ms': sum(gaps) if len(gaps) == len(rows) else None,
            'sampled_heap_flat': memory_flat if memory_known else None}


def compare(baseline_paths, candidate_paths, min_trials=5):
    seen = set()
    ac, a, af, ah = load_runs(baseline_paths, seen)
    bc, b, bf, bh = load_runs(candidate_paths, seen)
    if ac != bc:
        raise ValueError('baseline/candidate contexts differ (board, rig, observer, workload bytes or settings)')
    results, failed, incomplete, controlled_layers = {}, False, False, 0
    for case in sorted(a):
        ar, br = a[case], b[case]
        if set(ar[0]['layers']) != set(br[0]['layers']) or ar[0]['settings'] != br[0]['settings']:
            raise ValueError(f'{case}: baseline/candidate layers or settings differ')
        enough = min(len(ar), len(br)) >= min_trials
        incomplete |= not enough
        layers = {}
        for layer in ar[0]['layers']:
            av, bv = [r['layers'][layer] for r in ar], [r['layers'][layer] for r in br]
            rates = [[v['updates_per_second'] for v in values] for values in (av, bv)]
            costs = [[v['mean_render_us'] for v in values if v['mean_render_us'] is not None] for values in (av, bv)]
            cached = (case == 'control-color' or case in ('stars-background', 'saved-stars') and layer == 'A/layer0')
            entry = {'baseline_updates_hz': spread(rates[0]), 'candidate_updates_hz': spread(rates[1])}
            if cached:
                entry['verdict'] = 'pass' if all(v == 0 for r in rates for v in r) else 'fail'
                entry['gate'] = 'unchanged background remains cached after warmup'
            elif any(v <= 0 for r in rates for v in r) or any(len(c) != len(v) for c, v in zip(costs, (av, bv))):
                entry['verdict'] = 'fail'
                entry['gate'] = 'every dynamic layer completes work in every window'
            else:
                if case not in DIAGNOSTIC:
                    controlled_layers += 1
                rate_change = (statistics.median(rates[1]) / statistics.median(rates[0]) - 1) * 100
                cost_change = (statistics.median(costs[1]) / statistics.median(costs[0]) - 1) * 100
                entry.update(baseline_render_us=spread(costs[0]), candidate_render_us=spread(costs[1]),
                             updates_change_percent=rate_change, render_change_percent=cost_change)
                entry['verdict'] = ('diagnostic' if case in DIAGNOSTIC else 'incomplete' if not enough
                                    else 'fail' if rate_change < -5 or cost_change > 5 else 'pass')
            failed |= entry['verdict'] == 'fail'
            layers[layer] = entry
        sa, sb = service(ar), service(br)
        latency_known = min(sa['status_samples'], sb['status_samples']) >= 1000
        service_fail = sb['ingress_over_10ms'] is not None and sb['ingress_over_10ms'] > 0
        service_fail |= sb['sampled_heap_flat'] is False
        latency_exceeded = False
        if sb['usb_p95_ms'] is not None:
            latency_exceeded = sb['usb_p95_ms'] > 15 or sb['usb_p99_ms'] > 25
            if sa['usb_p95_ms'] is not None:
                latency_exceeded |= sb['usb_p95_ms'] > sa['usb_p95_ms'] + max(1, sa['usb_p95_ms'] * .1)
        service_fail |= latency_known and latency_exceeded
        service_known = (latency_known
                         and sb['ingress_over_10ms'] is not None and sb['sampled_heap_flat'] is not None)
        sv = 'fail' if service_fail else 'pass' if service_known else 'incomplete'
        failed |= service_fail
        incomplete |= not service_known
        results[case] = {'baseline_trials': len(ar), 'candidate_trials': len(br), 'layers': layers,
                         'baseline_service': sa, 'candidate_service': sb, 'service_verdict': sv,
                         'latency_limits_exceeded_in_samples': latency_exceeded}
    incomplete |= controlled_layers == 0
    return {'schema': 'lightvm.optimisation-comparison.v1', 'context': ac,
            'baseline_files': af, 'candidate_files': bf,
            'baseline_firmware_sha256': ah, 'candidate_firmware_sha256': bh,
            'verdict': 'fail' if failed else 'incomplete' if incomplete else 'pass',
            'scope': 'Recorded steady-state cases only; correctness, preparation, network stress, resource bounds, endurance and restoration of user playback require separate evidence.',
            'policy': {'min_trials': min_trials, 'max_rate_loss_percent': 5, 'max_render_increase_percent': 5,
                       'usb_p95_ms': 15, 'usb_p99_ms': 25, 'min_status_samples': 1000},
            'cases': results}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, nargs='+', required=True)
    parser.add_argument('--candidate', type=Path, nargs='+', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        result = compare(args.baseline, args.candidate)
    except (ValueError, KeyError, TypeError) as error:
        result = {'verdict': 'invalid', 'error': str(error)}
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + '\n')
    print(result['verdict'] + ': ' + str(args.output))
    raise SystemExit(0 if result['verdict'] == 'pass' else 1 if result['verdict'] == 'fail' else 2)


if __name__ == '__main__':
    main()
