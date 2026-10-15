"""Original Rainbow: rounded initial hue, duration-based phase and colorwheel.
color_count/color_n were unused by the original rendering calculation.
"""
from lightvm import profile, setting, rng, rgb, init, step, kernel, require, pico_colorwheel, hue_degrees, colorwheel, cycle_complete
profile("animation/1")
color = setting(rgb(255, 0, 0))
random_color = setting(True)
duration = setting(60.0, minimum=0.001)
scale = setting(1.0, minimum=0.0)
color_count = setting(30)
color_n = setting(1.5)
seed = setting(1, minimum=0)
update_us = setting(20_000, minimum=1_000, maximum=1_000_000)
random = rng(seed=seed, stream=0)
hue = 0.0
spatial_scale = 0.0
last_cycle = 0

@init
def setup():
    global hue, spatial_scale
    chosen_color = color
    if random_color:
        ok, hue_index = random.try_index(361)
        require(ok)
        chosen_color = pico_colorwheel(hue_index)
    hue = hue_degrees(chosen_color) * 255 / 360
    spatial_scale = scale * 255 / count

@step(period_us=update_us, first="immediate", max_catch_up=4, overload="slow")
def update():
    global last_cycle
    current_cycle = int(time / duration)
    if current_cycle > last_cycle:
        cycle_complete(count=current_cycle - last_cycle)
        last_cycle = current_cycle

@kernel
def draw():
    progress = (time % duration) / duration
    start_hue = hue + progress * 255
    for i in range(count):
        pixel_hue = start_hue + i * spatial_scale
        pixels[i] = colorwheel(pixel_hue)
