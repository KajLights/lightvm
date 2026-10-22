import math
from lightvm import profile, init, render, rgb, fixed, fixed_sqrt, FixedArray
profile("animation")

@init
def setup(state, runtime):
    state.table = FixedArray(runtime.pixel_count, fill=fixed(0.75) + fixed(0.75))
    state.factor = fixed(1.5) + fixed(1.5)

@render
def draw(state, runtime, pixels):
    for i in range(pixels.count):
        q = state.table[i]
        v = 0
        if i < 16:
            v = int(q * state.factor)
        else:
            v = int(fixed_sqrt(q))
        pixels[i] = rgb(v & 31, int(math.sqrt(4.0)), 7)
