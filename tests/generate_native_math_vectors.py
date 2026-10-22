"""Freeze exact binary32 inputs before evaluating native implementations."""
import json
from pathlib import Path
import random
import sys
import native_math_oracle as oracle

ROOT=Path(__file__).resolve().parents[1]


def generate():
    rng=random.Random(0x53234)
    words={0,0x80000000,1,0x80000001,0x007fffff,0x00800000,0x7f7fffff,0xff7fffff,
           0x7f800000,0xff800000,0x7fc00001,0xff800001}
    for exponent in range(255):
        for fraction in (0,rng.getrandbits(23)):
            for sign in (0,0x80000000):words.add(sign|(exponent<<23)|fraction)
    for center in (0x3f000000,0x3f800000,0x40000000,0x40490fdb,0x40c90fdb,0x42b17218,0xc2aeac50,0xc2cff1b4):
        words.update((center+n)&0xffffffff for n in range(-4,5))
    cases={(name,(word,)) for name in oracle.ULPS if name not in ('min','max','pow') for word in words}
    pairs=[(rng.getrandbits(32),rng.getrandbits(32)) for _ in range(512)]
    special=(0,0x80000000,1,0x80000001,0x3f000000,0x3f800000,0xbf800000,0x40000000,0xc0000000,0x40400000,0x7f800000,0x7fc00000)
    pairs.extend((a,b) for a in special for b in special)
    for name in ('min','max','pow'):cases.update((name,pair) for pair in pairs)
    for word in words:
        for exponent in (0x3f000000,0x3f800000,0x40000000,0xc0000000):cases.add(('pow',(word,exponent)))
    reviewed=json.loads((ROOT/'tests/fixtures/numeric-vectors.json').read_text())['native']
    cases.update((v['op'][4:],tuple(int(x,16) for x in v['args'])) for v in reviewed)
    vectors=[]
    for name,args in sorted(cases):
        result=oracle.stable_reference(name,args)
        row={'op':'f32.'+name,'args':[f'{word:08x}' for word in args]}
        row.update({'fault':True} if result is None else {'result':f'{result:08x}'})
        vectors.append(row)
    return {'method':'Independent Decimal/Fraction reference at 90 and 160 digits; Chudnovsky pi and Taylor sin/cos; exact binary32 rounding',
            'seed':0x53234,'max_ulp':oracle.ULPS,'vectors':vectors}


if __name__=='__main__':
    data=json.dumps(generate(),separators=(',',':'))+'\n'
    destination=Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/'tests/fixtures/native-math-vectors.json'
    destination.write_text(data)
    print(f'{len(json.loads(data)["vectors"])} vectors; {len(data)} bytes',flush=True)
