# Make the first light orange and the last light blue.
from lightvm import profile, rgb, render

profile("animation")

@render
def draw(state, runtime, pixels):
    pixels.fill(rgb(0, 0, 0))
    pixels[0] = rgb(40, 8, 0)
    pixels[(pixels.count - 1)] = rgb(0, 4, 40)
