#!/usr/bin/env python3
"""Report detector decisions adjacent to committed float32 thresholds.

The same PCM prefixes and settings run on native C, browser WASM and optionally
the physical S3 probe. Boundary disagreements are reported, never hidden by a
looser global tolerance. Separated controls must retain exact event hops.
The caller must restore the Sense application after flashing the USB-only probe.
"""
import argparse
import gzip
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import tempfile
import time

from check_audio_s3 import ROOT, DEFAULTS, Native, Probe, native_library, production_code
from audio_dependency import verify


def native_rows(library, pcm, case):
    native = Native(library, case['profile'], 16, {**DEFAULTS, 'onset_threshold': case['threshold']})
    try:
        return [native.push(pcm[hop*2048:(hop+1)*2048], (hop+1)*1024*1000000//48000)
                for hop in range(case['target_hop']+1)]
    finally:
        native.close()


def generate(library, pcm, sha):
    """Locate adjacent representable sensitivities with opposite native decisions.

    Calibration is explicit and committed; subsequent runs use the frozen cases.
    Each selected attack has eligible timing/percussion and a score in the public
    sensitivity range. Its earlier attacks stay well separated from this boundary.
    """
    bits = lambda value: struct.unpack('<I', struct.pack('<f', value))[0]
    number = lambda value: struct.unpack('<f', struct.pack('<I', value))[0]
    cases = []
    for profile, target in ((1, 386), (2, 281)):
        case = dict(profile=profile, target_hop=target)
        low, high = bits(.02), bits(.5)
        def event(threshold):
            return native_rows(library, pcm, {**case, 'threshold': threshold})[-1][73] > 0
        assert event(number(low)) and not event(number(high))
        while high-low > 1:
            middle = (low+high)//2
            if event(number(middle)):
                low = middle
            else:
                high = middle
        for offset in (-32, -1, 0, 1, 32):
            threshold = number(high+offset)
            cases.append({**case, 'name': f'profile{profile}_boundary_{offset:+d}ulp',
                          'threshold': threshold, 'threshold_f32_hex': f'{high+offset:08x}',
                          'kind': 'boundary', 'native_expected_event': offset < 0})
        for direction in (-1, 1):
            threshold = number(bits(number(high)+direction*1e-4))
            cases.append({**case, 'name': f'profile{profile}_separated_{direction:+d}',
                          'threshold': threshold, 'threshold_f32_hex': f'{bits(threshold):08x}',
                          'kind': 'separated', 'native_expected_event': direction < 0})
    return dict(schema=1, fixture_sha256=sha, bands=16, parameters=DEFAULTS,
                target_hop_indexing='zero-based; input is the corpus prefix through this hop', cases=cases)


def describe(rows):
    last = rows[-1]
    return dict(event_hops=[hop for hop, row in enumerate(rows) if row[73] > 0],
                target=dict(novelty=last[71], threshold=last[72], margin=last[71]-last[72],
                            strength=last[73], event=last[73] > 0))


def compare(reference, actual, case):
    assert len(reference) == len(actual)
    maxima = [0.0]*74
    for hop, (expected, observed) in enumerate(zip(reference, actual)):
        assert expected[0] == observed[0], ('readiness', case['name'], hop)
        for index, (a, b) in enumerate(zip(expected, observed)):
            assert math.isfinite(a) and math.isfinite(b)
            maxima[index] = max(maxima[index], abs(a-b))
        # Threshold sensitivity affects the onset envelope, not calibrated
        # level/bass/mid/treble/centroid or the spectrum.
        for index in (*range(1, 6), *range(7, 71)):
            assert abs(expected[index]-observed[index]) <= (1e-3 if index == 5 else 1e-4), (case['name'], hop, index)
        if case['kind'] == 'separated' or hop < case['target_hop']:
            assert (expected[73] > 0) == (observed[73] > 0), ('non-boundary occurrence differs', case['name'], hop)
            assert abs(expected[6]-observed[6]) <= 1e-4, ('non-boundary envelope differs', case['name'], hop)
    return dict(calibrated_metrics_max=max(maxima[i] for i in (*range(1, 5), *range(7, 71))),
                centroid_max=maxima[5], onset_envelope_max=maxima[6], novelty_max=maxima[71],
                threshold_max=maxima[72], strength_max=maxima[73])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', type=Path, required=True)
    parser.add_argument('--wasm', type=Path, default=ROOT/'build/audio/kaj-audio.mjs')
    parser.add_argument('--cases', type=Path, default=ROOT/'tests/audio-boundary-cases.json')
    parser.add_argument('--generate', action='store_true', help='explicitly recalibrate and save the frozen cases')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--port'); parser.add_argument('--firmware', type=Path)
    parser.add_argument('--application-build', type=Path)
    args = parser.parse_args()
    if args.port and (not args.firmware or not args.application_build):
        parser.error('--port requires --firmware and --application-build for machine-code verification')
    core = args.core.resolve()
    revision = verify(core)
    assert (args.wasm.parent/'revision.txt').read_text().strip() == revision
    pcm = gzip.decompress((core.parent/'tests/fixtures/conformance.pcm.gz').read_bytes())
    sha = hashlib.sha256(pcm).hexdigest()
    report = dict(core_revision=revision, fixture_sha256=sha, physical_s3=bool(args.port),
                  speaker_playback=False, microphone_capture=False, cases=[], result='incomplete')
    probe = None
    started = time.monotonic()
    try:
        if args.port:
            report['production_matching_code_sha256'] = production_code(args.application_build, args.firmware.parent)
            report['probe_sha256'] = hashlib.sha256(args.firmware.read_bytes()).hexdigest()
            report['application_sha256'] = hashlib.sha256((args.application_build/'kaj_fi.bin').read_bytes()).hexdigest()
            probe = Probe(args.port)
            report['device'] = probe.info()
        with tempfile.TemporaryDirectory(prefix='audio-boundaries-') as directory:
            work = Path(directory)
            library = native_library(core, work)
            if args.generate:
                args.cases.write_text(json.dumps(generate(library, pcm, sha), indent=2)+'\n')
            manifest = json.loads(args.cases.read_text())
            assert manifest['fixture_sha256'] == sha
            assert manifest['parameters'] == DEFAULTS and manifest['bands'] == 16
            report['cases_sha256'] = hashlib.sha256(args.cases.read_bytes()).hexdigest()
            report['parameters'] = manifest['parameters']
            (work/'pcm').write_bytes(pcm)
            (work/'cases.json').write_text(json.dumps(manifest['cases']))
            subprocess.run(['node', str(ROOT/'tests/audio_boundary_fixture.mjs'), str(args.wasm.resolve()),
                            str(work/'pcm'), str(work/'cases.json'), str(work/'wasm.bin')], check=True)
            wasm = iter(struct.iter_unpack('<74f', (work/'wasm.bin').read_bytes()))
            for case in manifest['cases']:
                reference = native_rows(library, pcm, case)
                # Different native libm/architectures may disagree at the same
                # frozen boundary too; only separated controls promise equality.
                if case['kind'] == 'separated':
                    assert (reference[-1][73] > 0) == case['native_expected_event'], ('separated native decision changed', case['name'])
                assert struct.pack('<f', case['threshold']).hex() == bytes.fromhex(case['threshold_f32_hex'])[::-1].hex()
                observed = [next(wasm) for _ in reference]
                entry = {**case, 'native': describe(reference), 'wasm': describe(observed),
                         'native_differs_from_calibration': (reference[-1][73] > 0) != case['native_expected_event'],
                         'wasm_errors': compare(reference, observed, case)}
                report['cases'].append(entry)
                if probe:
                    probe.start(case['profile'], 16, {**DEFAULTS, 'onset_threshold': case['threshold']})
                    actual = [probe.push(pcm[hop*2048:(hop+1)*2048], (hop+1)*1024*1000000//48000)
                              for hop in range(len(reference))]
                    entry['s3'] = describe(actual)
                    entry['s3_errors'] = compare(reference, actual, case)
                    entry['device'] = probe.info()
                    assert entry['device']['max_dsp_us'] < 21333
            assert next(wasm, None) is None
        report['decision_disagreements'] = [dict(case=case['name'], platform=platform,
            native=case['native']['target'], observed=case[platform]['target'])
            for case in report['cases'] for platform in ('wasm', 's3') if platform in case
            and case['native']['target']['event'] != case[platform]['target']['event']]
        report['result'] = 'passed; boundary decisions reported separately'
    finally:
        if probe:
            probe.serial.close()
        report['elapsed_seconds'] = time.monotonic()-started
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({key: value for key, value in report.items() if key != 'cases'}, indent=2))


if __name__ == '__main__':
    main()
