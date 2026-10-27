# Find a free light, including when no lights are free.
from lightvm import profile, rgb, init, render
from array import array

profile("animation")

@init
def start(state, runtime):
    state.used = array("i", [0] * runtime.pixel_count)

@render
def draw(state, runtime, pixels):
    state.used[0] = 1
    for position in range(pixels.count):
        if (state.used[int(position)] == 1):
            pixels[int(position)] = rgb(40, 8, 0)
        else:
            pixels[int(position)] = rgb(0, 4, 40)
