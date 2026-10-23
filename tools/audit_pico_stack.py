#!/usr/bin/env python3
"""Inventory optimised GCC call edges and stack frames, without hiding unknowns.

The default command rejects an incomplete callgraph: missing frames, indirect
calls, recursion and missing roots fail it. --report-only writes the same
incomplete evidence for review. This is an inventory, not a stack certificate:
interrupt/library/assembly allowances and indirect targets still require audit.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

QUOTED = r'"(?:[^"\\]|\\.)*"'
NODE = re.compile(r'node:\s*\{ title:\s*(' + QUOTED + r') label:\s*(' + QUOTED + r')')
EDGE = re.compile(r'edge:\s*\{ sourcename:\s*(' + QUOTED + r') targetname:\s*(' + QUOTED + r')')
FRAME = re.compile(r'\n(\d+) bytes \(([^)]+)\)$')
ROOTS = ('main', 'app_init', 'app_dispatch', 'session_feed', 'app_tick',
         'src/home_assistant.c:message', 'ha_tick', 'src/ha_keys.c:indicator_request',
         'platforms/pico/render_executor.c:worker_main', 'animation_execute',
         'control_prepare', 'control_reset_program', 'store_put')


def cmake_enabled(value):
    """Match CMake's false constants for a defined cache variable."""
    value = value.upper()
    return value not in ('', '0', 'OFF', 'NO', 'FALSE', 'N', 'IGNORE', 'NOTFOUND') and not value.endswith('-NOTFOUND')


def parse_graph(texts):
    frames, edges, kinds = {}, {}, {}
    for text in texts:
        for title, label in NODE.findall(text):
            name, label = json.loads(title), json.loads(label)
            match = FRAME.search(label)
            if match:
                frames[name] = max(frames.get(name, 0), int(match[1]))
                kinds[name] = match[2]
        for caller, target in EDGE.findall(text):
            edges.setdefault(json.loads(caller), set()).add(json.loads(target))
    return frames, edges, kinds


def audit(frames, edges, kinds, roots=ROOTS):
    results = {}
    for root in roots:
        matches = [name for name in frames if name == root or name.endswith('/' + root)]
        if not matches:
            results[root] = {'complete': False, 'unresolved': ['missing root: ' + root], 'known_frame_sum_bytes': None}
            continue
        memo = {}

        def visit(name, active):
            if name in active:
                return 0, [], {'recursive edge: ' + name}
            if name in memo:
                return memo[name]
            if name not in frames:
                return 0, [name], {'missing/indirect frame: ' + name}
            unknown = set()
            if kinds[name] != 'static':
                unknown.add('non-static frame: ' + name + ' (' + kinds[name] + ')')
            maximum, path = 0, []
            for target in sorted(edges.get(name, ())):
                total, branch, gaps = visit(target, active | {name})
                unknown.update(gaps)
                if total > maximum:
                    maximum, path = total, branch
            result = frames[name] + maximum, [name] + path, unknown
            memo[name] = result
            return result

        longest, path, unresolved = 0, [], set()
        for name in matches:
            total, branch, unknown = visit(name, set())
            unresolved.update(unknown)
            if total > longest:
                longest, path = total, branch
        results[root] = {'complete': not unresolved, 'known_frame_sum_bytes': longest,
                         'known_path': path, 'unresolved': sorted(unresolved)}
    return {'certified': False, 'interrupt_allowance_bytes': None,
            'note': 'Direct-edge inventory only. Indirect targets and interrupt/library/assembly bounds require audit; known-frame sums are not complete stack bounds.',
            'frame_count': len(frames), 'edge_count': sum(map(len, edges.values())), 'roots': results}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--report-only', action='store_true')
    parser.add_argument('--elf',type=Path)
    parser.add_argument('--objdump',type=Path)
    args = parser.parse_args()
    files = sorted(args.build.rglob('*.ci'))
    if not files:
        parser.error('no GCC callgraph files; build with -fcallgraph-info=su')
    # Retain source identity without baking a developer's checkout into reports.
    root = str(Path(__file__).resolve().parents[1]) + '/'
    texts = [file.read_text().replace(root, '') for file in files]
    if args.elf:
        if not args.objdump:parser.error('--elf requires --objdump')
        from pico_stack_paths import account
        cache={line.split(':',1)[0]:line.split('=',1)[1] for line in (args.build/'CMakeCache.txt').read_text().splitlines() if re.match(r'[^/#][^:]*:[^=]+=',line)}
        features={key:cmake_enabled(cache[key]) for key in ('LIGHTVM_WIFI','LIGHTVM_HTTP','LIGHTVM_MQTT')}
        report=account(*parse_graph(texts),args.objdump,args.elf,ROOTS,features)
        from pico_stack_fingerprints import verify
        report['contract_identity']=verify(args.build)
        if not report['contract_identity']['complete']:
            report['software_accounting_complete']=False
            report['software_gate_passed']=False
    else:report = audit(*parse_graph(texts))
    report['objects'] = len(files)
    report['callgraph_sha256'] = hashlib.sha256('\n'.join(texts).encode()).hexdigest()
    args.output.write_text(json.dumps(report, indent=2).replace(root, '') + '\n')
    print(f'Pico stack accounting: {len(files)} objects, {report["frame_count"]} frames; software gate {report.get("software_gate_passed",False)}; hardware validation pending')
    if not args.report_only and (not report.get('software_gate_passed',False)):
        problems=set(report.get('contract_identity',{}).get('errors',()))
        for group in ('roots','interrupt_paths'):
            for result in report.get(group,{}).values():
                problems.update(result['unresolved'])
        for stack in ('main','worker'):
            result=report.get(stack,{})
            if result and result['remaining_bytes']<result['required_margin_bytes']:
                problems.add(f'{stack} stack margin: {result["remaining_bytes"]} bytes; required {result["required_margin_bytes"]}')
        for problem in sorted(problems):print('  '+problem)
        parser.exit(1, f'Stack path audit failed. Full report: {args.output}\n')


if __name__ == '__main__':
    main()
