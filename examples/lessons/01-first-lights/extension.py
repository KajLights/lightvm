# Make the first light orange and the last light blue.
from lightvm import profile, rgb, render

profile("animation")

@render
def draw(state, runtime, pixels):
    pixels.fill(rgb(0, 0, 0))
    pixels[int(int((pixels.count / 2.0)))] = rgb(30, 30, 30)
