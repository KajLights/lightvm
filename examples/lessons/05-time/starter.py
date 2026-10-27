# Make a light blink once per second while the controls stay usable.
from lightvm import profile, rgb, render

profile("animation")

@render
def draw(state, runtime, pixels):
    pixels.fill(rgb(40, 8, 0))
