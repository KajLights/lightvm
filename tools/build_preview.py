#!/usr/bin/env python3
"""Build the controller VM for the local editor. Requires Emscripten 4.0.7+."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
from audio_dependency import materialize, verify

ROOT = Path(__file__).resolve().parents[1]


def build(emcc, if_needed=False):
    core = Path(os.environ['KAJ_AUDIO_CORE']) if os.environ.get('KAJ_AUDIO_CORE') else materialize(ROOT / '.deps/kaj-fi')
    verify(core)
    destination = ROOT / 'build/editor-assets/preview'
    destination.mkdir(parents=True, exist_ok=True)
    generated = ROOT / 'build/browser-generated'
    generated.mkdir(parents=True, exist_ok=True)
    release = json.loads((ROOT / 'lightvm/release.json').read_text())
    (generated / 'lightvm_release.h').write_text(
        '#define LIGHTVM_VERSION ' + json.dumps(release['version']) + '\n' +
        '#define LIGHTVM_BASELINE ' + json.dumps(release['baseline']) + '\n')
    sources = ['platforms/browser/preview.c'] + ['src/' + name + '.c' for name in (
        'portable', 'portable_map', 'portable_draw', 'portable_manifest', 'animation',
        'bytecode_core', 'numeric', 'native_math', 'noise', 'draw', 'render_executor',
        'vm_allocator', 'inputs', 'audio_sources', 'release', 'outputs')]
    sources += [str(core / name) for name in ('protocol.c', 'subscriptions.c', 'subscriber.c')]
    sources += ['shared/device-core/src/' + name + '.c' for name in ('json', 'version')]
    dependencies = sorted({ROOT / name for name in sources} | set((ROOT / 'src').glob('*.h')) |
                          set((ROOT / 'src').glob('*.inc')) | set((ROOT / 'shared/device-core/include').rglob('*.h')) |
                          set(core.glob('*.h')) |
                          {Path(__file__).resolve(), ROOT / 'tools/audio_dependency.py', ROOT / 'kaj-audio.rev',
                           ROOT / 'platforms/browser/board_config.h', ROOT / 'lightvm/release.json'})
    signature = hashlib.sha256(b''.join(path.read_bytes() for path in dependencies)).hexdigest()
    stamp = destination / 'source.sha256'
    if if_needed and stamp.exists() and stamp.read_text() == signature and all((destination / name).exists() for name in ('lightvm-preview.mjs', 'lightvm-preview.wasm')):
        return destination
    command = [emcc, '-O2', '-std=c11', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
               '-Isrc', '-Ishared/device-core/include', '-I' + str(generated), '-I' + str(core),
               '-DLIGHTVM_BOARD_CONFIG="' + str(ROOT / 'platforms/browser/board_config.h') + '"',
               *sources, '-lm', '-sMODULARIZE=1', '-sEXPORT_ES6=1', '-sENVIRONMENT=web,worker,node',
               '-sALLOW_MEMORY_GROWTH=1', '-sMAXIMUM_MEMORY=33554432', '-sSTACK_SIZE=262144',
               '-sEXPORTED_FUNCTIONS=["_malloc","_free"]',
               '-sEXPORTED_RUNTIME_METHODS=["HEAPU8","UTF8ToString"]',
               '-o', str(destination / 'lightvm-preview.mjs')]
    try: subprocess.run(command, cwd=ROOT, check=True)
    except FileNotFoundError:
        raise SystemExit('The browser preview needs Emscripten. Install emsdk 4.0.7 or newer and set EMCC to its emcc executable; see docs/teaching.md.')
    stamp.write_text(signature)
    print(destination)
    return destination


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emcc', default=os.environ.get('EMCC', 'emcc'))
    parser.add_argument('--if-needed', action='store_true')
    args = parser.parse_args()
    build(args.emcc, args.if_needed)
