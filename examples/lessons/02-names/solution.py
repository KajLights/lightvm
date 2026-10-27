# Change several lights together without hunting for repeated colour numbers.
from lightvm import profile, rgb, render

profile("animation")

@render
def draw(state, runtime, pixels):
    colour = rgb(40, 8, 0)
    pixels[0] = colour
    pixels[(pixels.count - 1)] = colour
