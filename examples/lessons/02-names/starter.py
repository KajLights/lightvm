# Change several lights together without hunting for repeated colour numbers.
from lightvm import profile, rgb, render

profile("animation")

@render
def draw(state, runtime, pixels):
    pixels[0] = rgb(40, 8, 0)
    pixels[(pixels.count - 1)] = rgb(40, 8, 0)
