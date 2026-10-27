# This version has a mistake. Find it and explain your repair.
from lightvm import profile, rgb, init, render

profile("animation")

@init
def start(state, runtime):
    state.position = 0.0
    state.speed = 8.0

@render
def draw(state, runtime, pixels):
    state.position = ((state.position + 1.0) % pixels.count)
    pixels[int(int(state.position))] = rgb(40, 8, 0)
