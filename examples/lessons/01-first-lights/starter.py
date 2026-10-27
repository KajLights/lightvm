# Make the first light orange and the last light blue.
from lightvm import profile, rgb, render

profile("animation")

@render
def draw(state, runtime, pixels):
    pixels[0] = rgb(40, 8, 0)
