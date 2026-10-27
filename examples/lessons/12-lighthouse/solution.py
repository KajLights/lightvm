# Build a lighthouse beam that crosses the ring seam, then explain how it works.
from lightvm import profile, rgb, init, render
from lightvm.motion import wrap_index

profile("animation")

def draw_beam(state, runtime, pixels, front):
    for behind in range(3):
        position = (int(front) - behind)
        if (behind == 0):
            colour = rgb(40, 20, 0)
        else:
            colour = rgb(10, 2, 0)
        pixels[int(wrap_index(int(position), pixels.count))] = colour

@init
def start(state, runtime):
    state.position = 0.0
    state.speed = 8.0

@render
def draw(state, runtime, pixels):
    state.position = ((state.position + (runtime.delta * state.speed)) % pixels.count)
    draw_beam(state, runtime, pixels, state.position)
