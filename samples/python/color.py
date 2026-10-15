"""Original Color: choose the constructor colour once, then fill every render."""
from lightvm import profile, setting, rng, rgb, init, kernel, require, pico_colorwheel
profile("animation/1")
color = setting(rgb(255, 64, 0))
random_color = setting(True)
seed = setting(1, minimum=0)
random = rng(seed=seed, stream=0)
chosen_color = rgb(0, 0, 0)

@init
def setup():
    global chosen_color
    chosen_color = color
    if random_color:
        ok, hue = random.try_index(361)
        require(ok)
        chosen_color = pico_colorwheel(hue)

@kernel
def draw():
    pixels.fill(chosen_color)
