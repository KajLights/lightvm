#!/usr/bin/env python3
"""Summarise raw matched runs without conflating useful frames and presentations."""
import argparse
import json
from pathlib import Path
import random
import statistics

from benchmark_saved_preset import percentile


def summarize(paths):
    groups = {}
    for path in paths:
        document = json.loads(path.read_text())
        for sample in document['samples']:
            sample['_presentation_valid'] = bool(sample.get('presentation_counter_path') or
                sample.get('case', {}).get('ab') or
                document.get('workload', '').startswith('unchanged saved preset '))
            groups.setdefault(sample.get('name', document.get('workload', 'saved')), []).append(sample)
    result = {}
    for name, samples in groups.items():
        layer_keys = sorted(samples[0]['layers'])
        layers = {}
        for layer in layer_keys:
            rows = [s['layers'][layer] for s in samples]
            values = [r['updates_per_second'] for r in rows]
            distinct = [(r['after']['distinct_presentations'] - r['before']['distinct_presentations']) / s['seconds']
                        for s, r in zip(samples, rows) if 'distinct_presentations' in r['before']]
            layers[layer] = {'median_updates_per_second': statistics.median(values),
                             'median_distinct_presentations_per_second': statistics.median(distinct) if distinct else None,
                             'min': min(values), 'max': max(values), 'samples': values,
                             'median_execution_us_per_second': statistics.median(r['execution_us_per_second'] for r in rows),
                             'median_render_us': statistics.median(r['mean_render_us'] for r in rows if r['mean_render_us'] is not None)
                             if any(r['mean_render_us'] is not None for r in rows) else None}
        latencies = [v for s in samples for v in s['status_latency_ms']['samples']]
        diagnostics = {}
        for layer in layer_keys:
            slot, ident = layer.split('/')
            observations = [row for s in samples for d in s['diagnostics']
                            for row in d['transition'][slot].get('layers', []) if row['id'] == ident]
            fields = {}
            # dirty_to_display_us is the last completed event, not a fresh
            # event on every poll. Event delays are measured by the input test.
            for key in ('displayed_sample_age_us', 'completion_to_display_us', 'dirty_age_us'):
                values = [r[key] for r in observations if r.get(key) is not None]
                if values:
                    fields[key] = {'count': len(values), 'p50': percentile(values, .5), 'p95': percentile(values, .95),
                                   'p99': percentile(values, .99), 'max': max(values)}
            if fields:
                diagnostics[layer] = fields
        pairs = []
        for sample in samples:
            before, after = sample.get('description_before', {}), sample['description_after']
            if 'render_epoch_total_us' in before:
                total = after['render_epoch_total_us'] - before['render_epoch_total_us']
                pairs.append({'epoch_us': total, 'join_wait_us': after['render_join_wait_us'] - before['render_join_wait_us'],
                              'pair_imbalance_us': after['render_pair_imbalance_us'] - before['render_pair_imbalance_us'],
                              'worker_job_us': after['render_worker_us'] - before['render_worker_us'],
                              'worker_job_wall_fraction': (after['render_worker_us'] - before['render_worker_us']) / (sample['seconds'] * 1e6),
                              'offloads': after['render_offloads'] - before['render_offloads'],
                              'steady_foreground_over_10ms': sample['foreground_gap_samples'][-1]['over_10ms'] - sample['foreground_gap_samples'][0]['over_10ms']
                              if sample.get('foreground_gap_samples') else None,
                              'steady_ingress_over_10ms': sample['foreground_gap_samples'][-1]['ingress_over_10ms'] - sample['foreground_gap_samples'][0]['ingress_over_10ms']
                              if sample.get('foreground_gap_samples') and sample['foreground_gap_samples'][0].get('ingress_over_10ms') is not None else None,
                              'foreground_over_10ms': after['foreground_over_10ms'] - before['foreground_over_10ms']
                              if 'foreground_over_10ms' in before else None})
        presentations = [s['presentations_per_second'] for s in samples if s['_presentation_valid']]
        result[name] = {'trials': len(samples), 'seconds': sum(s['seconds'] for s in samples), 'layers': layers,
                        'median_presentations_per_second': statistics.median(presentations) if presentations else None,
                        'valid_presentation_trials': len(presentations),
                        'status_latency_ms': {'count': len(latencies), 'p50': percentile(latencies, .5),
                                              'p95': percentile(latencies, .95), 'p99': percentile(latencies, .99),
                                              'max': max(latencies)} if latencies else {'count': 0},
                        'diagnostics': diagnostics, 'pair_measurements': pairs,
                        'max_observed_main_stack_bytes': max(s['description_after'].get('main_stack_peak_bytes', 0) for s in samples) or None,
                        'max_observed_worker_stack_bytes': max(s['description_after'].get('worker_stack_peak_bytes', 0) for s in samples) or None}
    return result


def compare(baseline, candidate):
    rng = random.Random(20261022)
    result = {}
    for name in sorted(baseline.keys() & candidate.keys()):
        rows = {}
        for layer in sorted(baseline[name]['layers'].keys() & candidate[name]['layers'].keys()):
            a, b = baseline[name]['layers'][layer]['samples'], candidate[name]['layers'][layer]['samples']
            if statistics.median(a) == 0:
                continue
            draws = []
            for _ in range(5000):
                x, y = statistics.median(rng.choices(a, k=len(a))), statistics.median(rng.choices(b, k=len(b)))
                if x:
                    draws.append((y / x - 1) * 100)
            rows[layer] = {'median_change_percent': (statistics.median(b) / statistics.median(a) - 1) * 100,
                           'bootstrap_trial_medians_95_percent': [percentile(draws, .025), percentile(draws, .975)]}
        result[name] = rows
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, action='append', required=True)
    parser.add_argument('--candidate', type=Path, action='append', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    a, b = summarize(args.baseline), summarize(args.candidate)
    args.output.write_text(json.dumps({'baseline': a, 'candidate': b, 'changes': compare(a, b),
                                      'interval_method': '5000 independent trial-resampling bootstrap draws of the median ratio; seed 20261022; sparse five-trial intervals are descriptive'}, indent=2) + '\n')
