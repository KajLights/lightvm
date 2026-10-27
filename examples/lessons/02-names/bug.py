# This version has a mistake. Find it and explain your repair.
from lightvm import profile, rgb, render

profile("animation")

@render
def draw(state, runtime, pixels):
    colour = rgb(40, 8, 0)
    colour = 0.5
    pixels[0] = colour
    pixels[(pixels.count - 1)] = colour
