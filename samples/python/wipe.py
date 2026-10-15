"""Original two-ended Wipe, including its draw-count completion boundary.
The standalone port explicitly completes and holds after that boundary.
step_width is the original constructor's 'step' argument.
"""
from lightvm import profile, setting, pixel_buffer, rgb, init, step, kernel, cycle_complete, complete
profile("animation/1")
color = setting(rgb(255, 64, 0))
step_width = setting(1, minimum=1, maximum=count)
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
    cycle = draw_count - 1
    if cycle > count / (2 * step_width):
        cycle_complete()
        complete()
        return
    start = cycle * step_width
    paint.fill(color, start=start, stop=start + step_width)
    start = count - cycle * step_width - step_width
    paint.fill(color, start=start, stop=start + step_width)

@kernel
def draw():
    pixels.copy_from(paint)
