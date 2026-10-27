# This version has a mistake. Find it and explain your repair.
from lightvm import profile, rgb, init, render

profile("animation")

def draw_beam(state, runtime, pixels, front):
    for behind in range(3):
        position = (int(front) - behind)
        if (behind == 0):
            colour = rgb(40, 20, 0)
        else:
            colour = rgb(10, 2, 0)
        pixels[int(position)] = colour

@init
def start(state, runtime):
    state.position = 0.0
    state.speed = 8.0

@render
def draw(state, runtime, pixels):
    state.position = ((state.position + (runtime.delta * state.speed)) % pixels.count)
    draw_beam(state, runtime, pixels, state.position)
