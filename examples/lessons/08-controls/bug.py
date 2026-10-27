# This version has a mistake. Find it and explain your repair.
from lightvm import profile, Input, rgb, render

profile("animation")
input_progress = Input("progress", default=0.5)

@render
def draw(state, runtime, pixels):
    pixels[int((int((runtime.input(input_progress) * pixels.count)) + 1))] = rgb(40, 8, 0)
