#!/usr/bin/env python3
"""Check CDC packet boundaries and fragmented requests on an attached controller."""
import argparse
import board_monitor
import hashlib
import json
from pathlib import Path
import time

from client import Client, MANAGE, SerialTransport, frame


def run(args):
    client = Client(SerialTransport(args.port, no_reset=args.no_reset), timeout=5)
    board_monitor.attach(client, args)
    result = {'label': args.label,
              'firmware_sha256': hashlib.sha256(args.firmware.read_bytes()).hexdigest(),
              'requests': []}
    try:
        before = client.describe()
        expected = client.get_config()
        body = b'{"op":"config_get"}'
        # Header is 16 bytes. Exact full-packet endings must complete without
        # another request or a host zero-length packet to release them.
        for fragmented in (False, True):
            for repeat in range(args.samples):
                for wire_bytes in (*range(64, 513, 64), 528):
                    payload = body.ljust(wire_bytes - 16, b' ')
                    if getattr(client, "board_monitor", None): client.board_monitor.check()
                    started = time.monotonic()
                    if fragmented:
                        client.request_id = client.request_id % 65535 + 1
                        client.data_reply.clear()
                        wire = frame(MANAGE, client.request_id, payload)
                        offset = 0
                        for size in (1, 14, 1, 47, 64, 193, len(wire)):
                            part = wire[offset:offset + size]
                            if not part:
                                break
                            client.transport.write(part)
                            offset += len(part)
                            time.sleep(.002)
                        reply = client.receive_reply(client.request_id)
                    else:
                        reply = client.command(MANAGE, payload)
                    client.require_ok(reply)
                    elapsed = (time.monotonic() - started) * 1000
                    if json.loads(client.data_reply) != expected:
                        raise RuntimeError('fragmented or packet-aligned response changed configuration')
                    result['requests'].append({'wire_bytes': wire_bytes, 'fragmented': fragmented,
                                               'repeat': repeat + 1, 'latency_ms': elapsed})
                args.result.write_text(json.dumps(result, indent=2) + '\n')
        after = client.describe()
        if before['boot_id'] != after['boot_id']:
            raise RuntimeError('controller rebooted during USB boundary checks')
        result.update(complete=True, boot_unchanged=True, boot_id=after['boot_id'],
                      config_unchanged=client.get_config() == expected)
        args.result.write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps({'requests': len(result['requests']), 'complete': True}), flush=True)
    finally:
        client.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--firmware', required=True, type=Path)
    parser.add_argument('--label', required=True)
    parser.add_argument('--result', required=True, type=Path)
    parser.add_argument('--samples', type=int, default=10)
    parser.add_argument('--no-reset', action='store_true')
    board_monitor.add_arguments(parser)
    args = parser.parse_args()
    if args.samples < 1:
        parser.error('at least one sample required')
    run(args)
