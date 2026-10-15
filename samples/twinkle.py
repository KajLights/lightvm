"""Stars appear at free positions and fade over a fixed background.

Spawn precedes expiry, with at most one birth per render. Occupancy flags and
free-position ranking avoid retry loops, including when every pixel is occupied.
Star ages advance by the actual render delta, independent of absolute uptime.
"""
import random
from array import array
from math import pi, sin

from lightvm import profile, setting, rgb, init, render, red, green, blue, pico_colorwheel

profile("animation/1")


@init
def setup(ctx, runtime):
    color = setting(rgb(255, 64, 0))
    random_background = setting(True)
    brightness = setting(0.05, minimum=0.0, maximum=1.0)
    ctx.twinkle_color = setting(rgb(255, 255, 255))
    ctx.random_stars = setting(True)
    ctx.duration = setting(2.0, minimum=0.001)
    ctx.probability = setting(2.0, minimum=0.0)
    max_stars = setting(max(1, runtime.pixel_count // 10), minimum=1, maximum=runtime.pixel_count)

    random.seed()
    base_color = color
    if random_background:
        base_color = pico_colorwheel(random.randint(0, 359))
    ctx.background = rgb(
        int(red(base_color) * brightness),
        int(green(base_color) * brightness),
        int(blue(base_color) * brightness),
    )

    ctx.star_age = array("f", [0.0] * max_stars)
    ctx.star_positions = array("I", [0] * max_stars)
    ctx.star_colors = array("I", [0] * max_stars)
    ctx.star_active = array("B", [0] * max_stars)
    ctx.occupied = array("B", [0] * runtime.pixel_count)
    ctx.active_count = 0


def choose_position(ctx):
    free_count = len(ctx.occupied) - ctx.active_count
    if free_count == 0:
        return -1
    rank = random.randrange(free_count)
    for position in range(len(ctx.occupied)):
        if not ctx.occupied[position]:
            if rank == 0:
                return position
            rank -= 1
    return -1


@render
def draw(ctx, runtime, pixels):
    for i in range(len(ctx.star_active)):
        if ctx.star_active[i]:
            ctx.star_age[i] += runtime.delta

    if random.random() < ctx.probability * runtime.delta:
        for i in range(len(ctx.star_active)):
            if not ctx.star_active[i]:
                position = choose_position(ctx)
                if position >= 0:
                    chosen_color = ctx.twinkle_color
                    if ctx.random_stars:
                        chosen_color = pico_colorwheel(random.randint(0, 359))
                    ctx.star_age[i] = 0.0
                    ctx.star_positions[i] = position
                    ctx.star_colors[i] = chosen_color
                    ctx.star_active[i] = 1
                    ctx.occupied[position] = 1
                    ctx.active_count += 1
                break

    for i in range(len(ctx.star_active)):
        if ctx.star_active[i] and ctx.star_age[i] > ctx.duration:
            ctx.star_active[i] = 0
            ctx.occupied[ctx.star_positions[i]] = 0
            ctx.active_count -= 1

    pixels.fill(ctx.background)
    for i in range(len(ctx.star_active)):
        if ctx.star_active[i]:
            fade = 1.0 - sin((ctx.star_age[i] / ctx.duration) * pi / 2.0)
            color = ctx.star_colors[i]
            r = max(red(ctx.background), red(color) * fade)
            g = max(green(ctx.background), green(color) * fade)
            b = max(blue(ctx.background), blue(color) * fade)
            pixels[ctx.star_positions[i]] = rgb(int(r), int(g), int(b))
