# Move a progress bar without editing and restarting the program.
from lightvm import profile, setting, Input, rgb, init, render

profile("animation")
input_progress = Input("progress", default=0.5)

@init
def start(state, runtime):
    state.warning = setting(0.8, minimum=0.0, maximum=1.0)

@render
def draw(state, runtime, pixels):
    if (runtime.input(input_progress) >= state.warning):
        pixels.fill(rgb(40, 0, 0))
    else:
        for position in range(pixels.count):
            if (position < (runtime.input(input_progress) * pixels.count)):
                pixels[int(position)] = rgb(40, 8, 0)
            else:
                pixels[int(position)] = rgb(0, 4, 40)
