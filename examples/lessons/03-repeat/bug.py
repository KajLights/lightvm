# This version has a mistake. Find it and explain your repair.
from lightvm import profile, rgb, render

profile("animation")

@render
def draw(state, runtime, pixels):
    for position in range(int((pixels.count + 1.0))):
        pixels[int(position)] = rgb(int((60.0 * (position / pixels.count))), 0, 0)
