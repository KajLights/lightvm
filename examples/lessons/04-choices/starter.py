# Split the strip into coloured sections, with a warning colour near the end.
from lightvm import profile, rgb, render

profile("animation")

@render
def draw(state, runtime, pixels):
    for position in range(pixels.count):
        if (position < (pixels.count / 2.0)):
            pixels[int(position)] = rgb(40, 8, 0)
        else:
            pixels[int(position)] = rgb(0, 4, 40)
