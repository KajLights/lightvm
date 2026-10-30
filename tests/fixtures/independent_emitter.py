#!/usr/bin/env python3
"""Independent LBC1/4 animation emitter, using docs/bytecode-emitter.md only.

Standard-library imports only. No compiler, catalogue loader, generated header,
or compiler-produced package is used. Keep the explicit wire IDs reviewable.
Run this file with an output directory to materialize the conformance corpus.
"""
import hashlib
import json
from pathlib import Path
import struct
import sys


def expected_pixels(count, rgba=True):
    return b''.join(bytes((4 if i < 16 else 1, 2, 7) + ((255,) if rgba else ()))
                    for i in range(count))


def emit(bulk=True, invalid=None, oversized=False, rgb=None):
    # (type, role, raw bits): the first three registers bind native arguments.
    regs = [(1, 1, 0), (2, 1, 0), (2, 1, 0), (6, 2, 0), (6, 2, 1),
            (1, 2, 0), (1, 2, 1), (1, 2, 31), (1, 2, 16),
            (7, 2, 98304), (1, 0, 0), (4, 0, 0), (7, 0, 0),
            (7, 0, 0), (1, 0, 0), (2, 2, 0x40800000), (2, 0, 0),
            (1, 0, 0), (1, 2, 7), (3, 0, 0), (7, 3, 0)]
    code = [(40, 20, 9, 9, 0), (0, 0, 0, 0, 0)]  # state = 1.5 + 1.5; END
    start = len(code)
    if bulk:
        code.append((39, 3, 10, 0, 0))  # output, local index, count, end (patched)
    else:
        code += [(2, 10, 5, 0, 0), (15, 11, 10, 0, 0), (18, 11, 0, 0, 0)]
    if invalid == 'map-work-budget': code += [(70,16,15,0,0)] * 8
    body = len(code)
    code += [(33, 12, 4, 10, 0),       # fixed array gather
             (15, 11, 10, 8, 0),      # index < 16
             (18, 11, 0, 0, body+6), # lazy alternate arm
             (42, 13, 12, 20, 0),     # 1.5 * 3.0
             (50, 14, 13, 0, 0),      # -> integer 4
             (19, 0, 0, 0, body+8),
             (55, 13, 12, 0, 0),      # sqrt(1.5)
             (50, 14, 13, 0, 0),      # -> integer 1
             (56, 14, 14, 7, 0),      # AND 31
             (68, 16, 15, 0, 0),      # f32 sqrt(4.0)
             (14, 17, 16, 0, 0),      # -> integer 2
             (20, 19, 14, 17, 18),    # RGB(r,2,7)
             (34, 3, 10, 19, 0)]      # one common output store
    if rgb is not None:
        channel, value = rgb
        regs.append((1,2,value&0xffffffff))
        instruction=list(code[body+11]);instruction[2+channel]=len(regs)-1
        code[body+11]=tuple(instruction)
    if bulk:
        code[start] = (39, 3, 10, 0, len(code))
    else:
        code += [(3, 10, 10, 6, 0), (19, 0, 0, 0, start+1)]
        code[start+2] = (18, 11, 0, 0, len(code))
    code.append((0, 0, 0, 0, 0))
    functions = [(0, start, 9, 0, 0, 3), (start, len(code), 13, 0, 0, 3)]
    # Expression 0 is pixel count; the remaining are literals and a fixed sum.
    expressions = [(1, 1, 0, 0, 0), (0, 3, 0, 0, 0), (0, 1, 0, 0, 0),
                   (0, 7, 0, 0, 49152), (40, 7, 3, 3, 0)]
    resources = [(12, 0, 1, 2, 2, 1), (20, 0, 4, 2, 2, 0)]
    if oversized:
        expressions.append((0, 1, 0, 0, 65535))
        resources[1] = (20, 5, 4, 2, 2, 0)
    requires = '0.1.0'
    if invalid == 'operand-type': code[body] = (33, 14, 4, 10, 0)
    elif invalid == 'resource-identity': regs[4] = (6, 2, 2)
    elif invalid == 'resource-flags': resources[1] = (20, 0, 4, 2, 2, 1)
    elif invalid == 'state-permission': functions[0] = (0, start, 8, 0, 0, 3)
    elif invalid == 'branch-target': code[body+2] = (18, 11, 0, 0, len(code)+1)
    elif invalid == 'map-uninitialized': code[body+2] = (18, 11, 0, 0, body+8)
    elif invalid == 'map-control-alias':
        regs[10] = (1, 0, 129)
        code[start] = (39, 3, 10, 10, len(code)-1)
    elif invalid == 'map-source-mutation': code[body+8] = (34, 4, 10, 12, 0)
    elif invalid == 'reserved-operand': code[body+9] = (68, 16, 15, 1, 0)
    elif invalid == 'map-work-budget': pass
    elif invalid == 'fixed-floor': requires = '0.0.0'
    elif invalid == 'kernel-floor': requires = '0.0.9'
    elif invalid == 'future-floor': requires = '99.0.0'
    elif invalid is not None: raise ValueError(invalid)
    manifest = json.dumps({'schema': 'lightvm.program', 'compiler_version': '0.1.0',
                           'requires': requires, 'profile': 'animation',
                           'pixels': {'format': 'rgba8-premul'}, 'settings': []},
                          separators=(',', ':')).encode()
    words = [4, len(regs), len(code), 0, 2, 2, len(expressions), 0, 0, 0, 1,
             0, 0, 0, 0, 0, 0, 3, len(manifest), 0, 0, 0, 0]
    package = b'LBC1' + struct.pack('<23I', *words)
    package += b''.join(struct.pack('<BBHI', t, role, 0, bits) for t, role, bits in regs)
    package += b''.join(struct.pack('<4BI', *instruction) for instruction in code)
    package += b''.join(struct.pack('<6I', *f) for f in functions)
    package += b''.join(struct.pack('<6I', *r) for r in resources)
    package += b''.join(struct.pack('<BBHIII', op, t, 0, a, b, v) for op, t, a, b, v in expressions)
    return package + manifest


INVALID = ('operand-type', 'resource-identity', 'resource-flags', 'state-permission',
           'branch-target', 'map-uninitialized', 'map-source-mutation', 'map-control-alias',
           'reserved-operand', 'map-work-budget', 'fixed-floor', 'kernel-floor', 'future-floor')


def corpus():
    return {'independent-scalar': emit(False), 'independent-bulk': emit(),
            **{'reject-'+name: emit(invalid=name) for name in INVALID}}


if __name__ == '__main__':
    destination = Path(sys.argv[1]); destination.mkdir(parents=True, exist_ok=True)
    records = []
    for name, package in corpus().items():
        (destination / (name+'.lbc')).write_bytes(package)
        records.append({'name': name, 'bytes': len(package),
                        'sha256': hashlib.sha256(package).hexdigest(),
                        'admitted': not name.startswith('reject-')})
    (destination/'independent.json').write_text(json.dumps(records, indent=2)+'\n')
    print(json.dumps(records, indent=2))
