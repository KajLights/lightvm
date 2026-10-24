#!/usr/bin/env python3
"""Exercise the normal controller API with identical bytecode on a controller and host.

Replaces the resident program, stops playback and restores active configuration.
Persistent programs and saved configuration are untouched.
"""
import argparse
import board_monitor
from pathlib import Path
import struct
import sys
import time
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from lightvm.compiler import compile_source
from client import Client, ProcessTransport, SerialTransport, ProtocolError, FRAME, PLAY, RESET, STOP


def check(condition,message):
    if not condition:raise ProtocolError(message)


def smoke(client, board, reference):
    info=client.describe()
    check(info['board']==board, 'unexpected controller identity')
    from lightvm.release import BASELINE, release_error
    check(release_error(info.get('version'), BASELINE) is None, 'compatible LightVM release required')
    original=client.get_config()
    count=min(100,info['pixel_capacity'])
    pin='D1' if board=='xiao_esp32c3' else 'D0' if board.startswith('xiao_esp32') else 'GP16'
    config={'schema':'lightvm.config','outputs':[{'id':'lights','type':'ws2812b','config':{'pin':pin,'count':count,'brightness_limit':32}}]}
    host_config={**config,'outputs':[{'id':'lights','type':'ws2812b','config':{'pin':'GP16','count':count}}]}
    configured_heap=None
    try:
        client.configure(config);reference.configure(host_config)
        configured_heap=client.status_fields()['heap']
        for source in sorted((ROOT/'examples/animations').glob('*.py')):
            artifact=compile_source(source.read_text().replace("random.seed()", "random.seed(1)"),str(source),info)
            check(artifact.success,str(artifact.diagnostics))
            client.upload(artifact.bytecode);reference.upload(artifact.bytecode)
            for delta in (0,16,17,1000):
                payload=struct.pack('<I',delta)
                client.require_ok(client.command(FRAME,payload));reference.require_ok(reference.command(FRAME,payload))
                check(client.pixel_snapshot==reference.pixel_snapshot,source.name+': output differs from host')
            before,pixels=client.snapshot();after,repeated=client.snapshot()
            # Live ages, dependency observations and coordinator waits can change
            # between requests while the paused program and pixels stay fixed.
            for snapshot in (before,after):
                snapshot['state']={key:value for key,value in snapshot['state'].items()
                                   if key not in ('displayed_sample_age_us','dirty_age_us','dependency_revision',
                                                  'runnable_wait_us','composite_wait_us','output_wait_us')}
            check(before==after and pixels==repeated,'snapshot changed program state')
            if source.stem=='color':
                client.set_inputs({'color':{'value':0x123456}})
                client.require_ok(client.command(FRAME,bytes(4)))
                check(bytes(client.pixel_snapshot)==bytes.fromhex('123456')*count,'live colour update failed')
            if source.stem=='sweep':
                state=client.program_state()
                check(state['render_count']>1,'Sweep did not keep rendering')
        # Use Rainbow to test autonomous playback and pause/resume.
        rainbow=compile_source((ROOT/'examples/animations/rainbow.py').read_text(),target=info)
        check(rainbow.success,str(rainbow.diagnostics));client.upload(rainbow.bytecode)
        client.require_ok(client.command(PLAY));time.sleep(.1)
        before=client.status_fields();time.sleep(.1);after=client.status_fields()
        check(after['mode']=='bytecode' and int(after['frames'])>int(before['frames']),'playback stopped')
        check(after['heap']==before['heap'],'playback allocation changed')
        client.pause_program();paused=client.program_state()['program_us'];time.sleep(.03)
        check(client.program_state()['program_us']==paused,'pause advanced time')
        client.resume_program()
        deadline=time.monotonic()+1
        while client.program_state()['program_us']<=paused:
            check(time.monotonic()<deadline,'resume failed')
            time.sleep(.02)
    finally:
        try:
            client.require_ok(client.command(STOP));client.require_ok(client.command(RESET))
            if configured_heap is not None:
                # Outputs and the scene-operation history retain their idle allocation.
                check(client.status_fields()['heap']==configured_heap,'RESET leaked interpreter memory')
        finally:
            client.configure(original)
    print('All effects, identical bytecode/pixels, controls, snapshots, playback, pause/resume and reset heap passed.')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    transport=parser.add_mutually_exclusive_group(required=True)
    transport.add_argument('--process',type=Path);transport.add_argument('--port')
    parser.add_argument('--board',required=True)
    parser.add_argument('--reference',type=Path,default=ROOT/'build/host/lightvm')
    parser.add_argument('--no-reset',action='store_true')
    board_monitor.add_arguments(parser)
    args=parser.parse_args()
    controller=SerialTransport(args.port,no_reset=args.no_reset or args.board.startswith('xiao_esp32')) if args.port else ProcessTransport(args.process)
    reference=ProcessTransport(args.reference)
    client=Client(controller)
    board_monitor.attach(client, args)
    try:smoke(client,args.board,Client(reference))
    finally:controller.close();reference.close()

if __name__=='__main__':main()
