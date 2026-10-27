# This version has a mistake. Find it and explain your repair.
from lightvm import profile, rgb, render

profile("animation")

def band_colour(position, split):
    if (position < split):
        colour = rgb(40, 8, 0)
    return colour

def draw_marker(state, runtime, pixels, position, colour):
    pixels[int(position)] = colour

@render
def draw(state, runtime, pixels):
    for position in range(pixels.count):
        pixels[int(position)] = band_colour(position, (pixels.count / 2.0))
    draw_marker(state, runtime, pixels, 0.0, rgb(30, 30, 30))
