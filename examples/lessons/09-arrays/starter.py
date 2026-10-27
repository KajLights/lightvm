# Give each light its own brightness and change one without changing the rest.
from lightvm import profile, hsv, init, render
from array import array

profile("animation")

@init
def start(state, runtime):
    state.brightness = array("f", [0.08] * runtime.pixel_count)

@render
def draw(state, runtime, pixels):
    for position in range(pixels.count):
        pixels[int(position)] = hsv(0.08, 1.0, state.brightness[int(position)])
