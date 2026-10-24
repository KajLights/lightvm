"""Independent binary64 equations for LightVM's seeded gradient fields.

Uses a weighted corner sum (Perlin) and sorted simplex axes, rather than the
runtime's staged blends and pairwise ranks. Inputs are quantized by the caller.
"""
import itertools
import math

DIRECTIONS = ((1,1,0),(-1,1,0),(1,-1,0),(-1,-1,0),
              (1,0,1),(-1,0,1),(1,0,-1),(-1,0,-1),
              (0,1,1),(0,-1,1),(0,1,-1),(0,-1,-1))


def mix(h):
    h = ((h ^ (h >> 16)) * 0x7feb352d) & 0xffffffff
    h = ((h ^ (h >> 15)) * 0x846ca68b) & 0xffffffff
    return h ^ (h >> 16)


def direction(vertex, seed):
    h = mix((seed & 0xffffffff) ^ 0x9e3779b9)
    for v, salt in zip(vertex, (0x85ebca6b, 0xc2b2ae35, 0x27d4eb2f)):
        h = mix(h ^ ((v * salt) & 0xffffffff))
    return DIRECTIONS[h % 12]


def sample(name, coords, seed=0, repeats=None):
    coords = [0.0 if abs(x) < 2**-126 else x for x in coords]
    dims = len(coords)
    if name.startswith('perlin'):
        cell = [math.floor(x) for x in coords]
        f = [x-i for x,i in zip(coords,cell)]
        fade = [6*t**5-15*t**4+10*t**3 for t in f]
        total = 0
        for corner in itertools.product((0,1), repeat=dims):
            vertex = [(i+c)%r for i,c,r in zip(cell,corner,repeats or [256]*dims)]
            g = direction(vertex+[0]*(3-dims),seed)
            total += sum(g[k]*(f[k]-corner[k]) for k in range(dims))*math.prod(
                fade[k] if corner[k] else 1-fade[k] for k in range(dims))
        return total/2
    skew = (math.sqrt(dims+1)-1)/dims
    unskew = (1-1/math.sqrt(dims+1))/dims
    cell = [math.floor(x+sum(coords)*skew) for x in coords]
    rel = [x-i+sum(cell)*unskew for x,i in zip(coords,cell)]
    order = sorted(range(dims), key=lambda k: (-rel[k],k))
    step = [0]*dims
    total = 0
    for corner in range(dims+1):
        delta = [rel[k]-step[k]+corner*unskew for k in range(dims)]
        radius = max(0,(0.5 if dims==2 else 0.6)-sum(x*x for x in delta))
        g = direction([cell[k]+step[k] for k in range(dims)]+[0]*(3-dims),seed)
        total += radius**4*sum(x*y for x,y in zip(delta,g))
        if corner < dims: step[order[corner]] = 1
    return max(-1,min(1,total*(70 if dims==2 else 32)))
