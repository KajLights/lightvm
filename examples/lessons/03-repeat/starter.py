# Paint a changing band of colour on any strip length without copying instructions.
from lightvm import profile, rgb, render

profile("animation")

@render
def draw(state, runtime, pixels):
    for position in range(pixels.count):
        pixels[int(position)] = rgb(40, 8, 0)
