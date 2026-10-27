# Make a light blink once per second while the controls stay usable.
from lightvm import profile, rgb, render

profile("animation")

@render
def draw(state, runtime, pixels):
    if ((int(runtime.time) % 2) == 0):
        pixels.fill(rgb(40, 8, 0))
    else:
        pixels.fill(rgb(0, 0, 0))
