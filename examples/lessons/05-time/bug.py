# This version has a mistake. Find it and explain your repair.
from lightvm import profile, rgb, render

profile("animation")

@render
def draw(state, runtime, pixels):
    while True:
        pixels.fill(rgb(40, 8, 0))
