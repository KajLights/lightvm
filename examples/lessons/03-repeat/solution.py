# Paint a changing band of colour on any strip length without copying instructions.
from lightvm import profile, rgb, render

profile("animation")

@render
def draw(state, runtime, pixels):
    for position in range(pixels.count):
        pixels[int(position)] = rgb(int((60.0 * (position / pixels.count))), 0, 0)
