# This version has a mistake. Find it and explain your repair.
from lightvm import profile, hsv, render

profile("animation")

@render
def draw(state, runtime, pixels):
    for position in range(pixels.count):
        pixels[int(position)] = hsv(0.08, 1.0, state.brightness[int(position)])
