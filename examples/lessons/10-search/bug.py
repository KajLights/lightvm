# This version has a mistake. Find it and explain your repair.
from lightvm import profile, rgb, init, render
from array import array

profile("animation")

@init
def start(state, runtime):
    state.used = array("i", [0] * runtime.pixel_count)

@render
def draw(state, runtime, pixels):
    slot = 0
    while ((slot < pixels.count) and (state.used[int(slot)] != 0)):
        slot = slot
    if (slot < pixels.count):
        state.used[int(slot)] = 1
    for position in range(pixels.count):
        if (state.used[int(position)] == 1):
            pixels[int(position)] = rgb(40, 8, 0)
        else:
            pixels[int(position)] = rgb(0, 4, 40)
