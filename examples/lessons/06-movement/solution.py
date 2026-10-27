# Move a dot at the same speed even when some frames arrive late.
from lightvm import profile, rgb, init, render

profile("animation")

@init
def start(state, runtime):
    state.position = 0.0
    state.speed = 8.0

@render
def draw(state, runtime, pixels):
    state.position = ((state.position + (runtime.delta * state.speed)) % pixels.count)
    pixels[int(int(state.position))] = rgb(40, 8, 0)
