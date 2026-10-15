"""Original Twinkle state and fade, with bounded free-position selection.
Spawn precedes expiry. Active flags replace the zero-time sentinel; dead stars
release their positions. One failed bounded RNG selection skips that birth.
"""
from lightvm import profile, setting, array, stamp, u16, u8, bool8, rng, rgb, init, step, kernel, now, elapsed, sin, red, green, blue, pico_colorwheel, require
profile("animation/1")
color = setting(rgb(255, 64, 0))
random_background = setting(True)
brightness = setting(0.05, minimum=0.0, maximum=1.0)
twinkle_color = setting(rgb(255, 255, 255))
random_stars = setting(True)
duration = setting(2.0, minimum=0.001)
probability = setting(2.0, minimum=0.0)
max_stars = setting(max(1, count // 10), minimum=1, maximum=count)
seed = setting(1, minimum=0)
update_us = setting(20_000, minimum=1_000, maximum=1_000_000)
star_birth_time = array(stamp, length=max_stars)
star_positions = array(u16, length=max_stars, fill=65535)
star_colors = array(u8, length=3 * max_stars)
star_active = array(bool8, length=max_stars)
occupied = array(bool8, length=count)
random = rng(seed=seed, stream=0)
background = rgb(0, 0, 0)
active_count = 0

@init
def setup():
    global background
    base_color = color
    if random_background:
        ok, hue = random.try_index(361)
        require(ok)
        base_color = pico_colorwheel(hue)
    background = rgb(int(red(base_color) * brightness), int(green(base_color) * brightness), int(blue(base_color) * brightness))

def choose_position():
    free_count = count - active_count
    if free_count == 0:
        return -1
    ok, rank = random.try_index(free_count)
    if not ok:
        return -1
    for position in range(count):
        if not occupied[position]:
            if rank == 0:
                return position
            rank -= 1
    return -1

@step(period_us=update_us, first="immediate", max_catch_up=4, overload="slow")
def update():
    global active_count
    spawn = random.uniform() < probability * delta
    if spawn:
        for i in range(max_stars):
            if not star_active[i]:
                position = choose_position()
                if position >= 0:
                    chosen_color = twinkle_color
                    ready = True
                    if random_stars:
                        ok, hue = random.try_index(361)
                        ready = ok
                        if ok:
                            chosen_color = pico_colorwheel(hue)
                    if ready:
                        star_birth_time[i] = now()
                        star_positions[i] = position
                        color_index = i * 3
                        star_colors[color_index] = red(chosen_color)
                        star_colors[color_index + 1] = green(chosen_color)
                        star_colors[color_index + 2] = blue(chosen_color)
                        star_active[i] = True
                        occupied[position] = True
                        active_count += 1
                break
    for i in range(max_stars):
        if star_active[i]:
            if elapsed(star_birth_time[i]) > duration:
                star_active[i] = False
                occupied[star_positions[i]] = False
                active_count -= 1

@kernel
def draw():
    pixels.fill(background)
    for i in range(max_stars):
        if star_active[i]:
            star_lifetime = elapsed(star_birth_time[i])
            fade = 1.0 - sin((star_lifetime / duration) * 3.141592653589793 / 2.0)
            color_index = i * 3
            r = max(red(background), star_colors[color_index] * fade)
            g = max(green(background), star_colors[color_index + 1] * fade)
            b = max(blue(background), star_colors[color_index + 2] * fade)
            pixels[star_positions[i]] = rgb(int(r), int(g), int(b))
