# Find a free light, including when no lights are free.
from lightvm import profile, rgb, init, render
from array import array

profile("animation")

@init
def start(state, runtime):
    state.used = array("i", [1] * runtime.pixel_count)

@render
def draw(state, runtime, pixels):
    slot = 0
    if (slot == pixels.count):
        pixels.fill(rgb(20, 20, 20))
