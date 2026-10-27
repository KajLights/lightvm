"""Source identity guard for manually audited stack callback/assembly contracts.

Recording is an explicit maintainer action after reviewing changed call targets,
assembly bounds and recursion guards. Firmware builds only verify; they never
regenerate this file to make a failing check pass.
"""
import argparse
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
MANIFEST=Path(__file__).with_name('pico-stack-contracts.json')
PROJECT=(
    'src/board_status.c','platforms/pico/board_status.c',
    'src/runtime.c','src/app.c','src/preparation.c','src/preparation.h','src/transition.c','src/transition.h','src/render_executor.c','src/vm.c',
    'src/device_host.c','src/store_policy.c',
    'src/mqtt.c','src/mqtt.h','src/home_assistant.c','src/home_assistant.h',
    'src/audio_sources.c','src/audio_sources.h','src/control.c','kaj-audio.rev','cmake/kaj_audio.cmake','src/inputs.c','src/inputs.h','src/outputs.c',
    'shared/device-core/src/http.c','shared/device-core/src/http_setup.c',
    'shared/device-core/src/websocket.c',
    'shared/device-core/src/store.c','shared/device-core/src/store_extent.c',
    'shared/device-core/src/network.c','shared/device-core/src/settings.c',
    'shared/device-core/src/discovery.c',
    'platforms/pico/render_executor.c','platforms/pico/network_port.c',
    'platforms/pico/main.c','platforms/pico/platform.c','platforms/pico/CMakeLists.txt',
    'platforms/pico/lwipopts.h','platforms/pico/tusb_config.h',
)

AUDIO_CORE=('features.h','onset.h','protocol.c','protocol.h','subscriptions.c','subscriptions.h','subscriber.c','subscriber.h')
def audio_core(build):
    cache=(build/'CMakeCache.txt').read_text()
    return next((Path(line.split('=',1)[1]) for line in cache.splitlines() if line.startswith('KAJ_AUDIO_CORE:')),ROOT/'.deps/kaj-fi/core')

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def dependencies(build,sdk):
    # These two targets supply the firmware sources and SDK objects. Native
    # pioasm/picotool sub-builds are packaging tools, not firmware stack paths.
    result=set()
    targets=[build/'CMakeFiles'/name for name in ('lightvm.dir','lightvm_core.dir')]
    targets.append(build/'device-core/CMakeFiles/device_core.dir')
    for target in targets:
        for file in target.rglob('*.o.d'):
            for word in file.read_text().replace('\\\n',' ').split():
                path=Path(word)
                if path.is_relative_to(sdk) and path.is_file():result.add(path.relative_to(sdk).as_posix())
    return result

def verify_files(manifest,sdk,root=ROOT):
    errors=[]
    for group,base in (('sdk',sdk),('project',root)):
        for name,expected in manifest[group].items():
            path=base/name
            if not path.is_file() or digest(path)!=expected:errors.append(f'changed audited {group} source: {name}')
    return errors

def verify(build):
    cache=(build/'CMakeCache.txt').read_text()
    sdk=next((Path(line.split('=',1)[1]) for line in cache.splitlines() if line.startswith('PICO_SDK_PATH:')),None)
    if sdk is None:return {'complete':False,'errors':['PICO_SDK_PATH missing from build cache']}
    manifest=json.loads(MANIFEST.read_text())
    errors=verify_files(manifest,sdk)
    core=audio_core(build)
    for name in AUDIO_CORE:
        path=core/name
        if not path.is_file() or digest(path)!=manifest.get('audio_core',{}).get(name):errors.append('changed audited audio core source: '+name)
    errors.extend('unaudited SDK dependency: '+name for name in sorted(dependencies(build,sdk)-manifest['sdk'].keys()))
    return {'complete':not errors,'manifest_sha256':digest(MANIFEST),'sdk_files':len(manifest['sdk']),
            'project_files':len(manifest['project']),'errors':errors}

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--record',action='store_true',required=True)
    parser.add_argument('sdk',type=Path)
    parser.add_argument('builds',type=Path,nargs='+')
    args=parser.parse_args();sdk=args.sdk.resolve()
    names=set().union(*(dependencies(build,sdk) for build in args.builds))
    if MANIFEST.exists():names.update(json.loads(MANIFEST.read_text())['sdk'])
    core=audio_core(args.builds[0])
    if any(audio_core(build).resolve()!=core.resolve() for build in args.builds):parser.error('builds use different audio core checkouts')
    if not names:parser.error('no SDK dependencies found in these builds')
    manifest={'note':'Reviewed configured SDK callback targets, assembly bounds and project recursion guards; see docs/design/pico-stack-accounting.md.',
              'sdk':{n:digest(sdk/n) for n in sorted(names)},
              'project':{n:digest(ROOT/n) for n in PROJECT if (ROOT/n).is_file()},
              'audio_core':{n:digest(core/n) for n in AUDIO_CORE}}
    MANIFEST.write_text(json.dumps(manifest,indent=2)+'\n')
    print(f'Recorded {len(names)} SDK dependencies and {len(manifest["project"])} project sources after review')
