"""Original Pattern: a 2..8-colour random palette, stretched to fit by default.
Set random_pattern=False and edit explicit_pattern for a supplied pattern.
Expand original run-length strings into this immutable tuple off-device.
"""
from lightvm import profile, setting, array, rgb8, rng, rgb, init, kernel, require, pico_colorwheel
profile("animation/1")
to_fit = setting(True)
random_pattern = setting(True)
seed = setting(1, minimum=0)
explicit_pattern = (rgb(255, 0, 0), rgb(0, 255, 0), rgb(0, 0, 255))
pattern = array(rgb8, length=8)
random = rng(seed=seed, stream=0)
pattern_length = 0

@init
def setup():
    global pattern_length
    pattern_length = len(explicit_pattern)
    if random_pattern:
        ok, length_index = random.try_index(7)
        require(ok)
        pattern_length = length_index + 2
        for i in range(pattern_length):
            ok, hue = random.try_index(361)
            require(ok)
            pattern[i] = pico_colorwheel(hue)

@kernel
def draw():
    scale = 1.0
    if to_fit:
        scale = count / pattern_length
    for p_ix in range(count):
        pattern_ix = int(p_ix / scale)
        if pattern_ix >= pattern_length:
            pattern_ix = pattern_ix % pattern_length
        if random_pattern:
            pixels[p_ix] = pattern[pattern_ix]
        else:
            pixels[p_ix] = explicit_pattern[pattern_ix]
