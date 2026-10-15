"""Original accumulating Chase. A cycle notification does not stop playback.
The old custom base omitted its colour binding; supply it explicitly here.
"""
from lightvm import profile, setting, pixel_buffer, rgb, init, step, kernel, cycle_complete
profile("animation/1")
color = setting(rgb(255, 64, 0))
update_us = setting(20_000, minimum=1_000, maximum=1_000_000)
paint = pixel_buffer(length=count)
draw_count = 0

@init
def setup():
    global draw_count
    draw_count = 0

@step(period_us=update_us, first="immediate", max_catch_up=4, overload="slow")
def update():
    global draw_count
    draw_count += 1
    cur_pix = (draw_count - 1) % count
    paint[cur_pix] = color
    if draw_count % count == 0:
        cycle_complete()

@kernel
def draw():
    pixels.copy_from(paint)
