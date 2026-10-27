# Move a progress bar without editing and restarting the program.
from lightvm import profile, rgb, render

profile("animation")

@render
def draw(state, runtime, pixels):
    for position in range(pixels.count):
        if (position < (0.5 * pixels.count)):
            pixels[int(position)] = rgb(40, 8, 0)
        else:
            pixels[int(position)] = rgb(0, 4, 40)
