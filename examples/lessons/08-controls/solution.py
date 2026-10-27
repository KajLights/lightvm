# Move a progress bar without editing and restarting the program.
from lightvm import profile, Input, rgb, render

profile("animation")
input_progress = Input("progress", default=0.5)

@render
def draw(state, runtime, pixels):
    for position in range(pixels.count):
        if (position < (runtime.input(input_progress) * pixels.count)):
            pixels[int(position)] = rgb(40, 8, 0)
        else:
            pixels[int(position)] = rgb(0, 4, 40)
