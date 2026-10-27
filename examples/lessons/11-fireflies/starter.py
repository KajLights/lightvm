# Make small lights appear, fade and leave room for the next ones.
from lightvm import profile, rgb, init, render, PixelArray, Transform
from lightvm.motion import wrap_index
import math
import random

profile("animation")

@init
def start(state, runtime):
    state.trail = PixelArray(runtime.pixel_count)
    random.seed(7)
    state.next_light = 0.0

@render
def draw(state, runtime, pixels):
    state.trail.multiply_alpha(math.exp((0.0 * runtime.delta)))
    if (runtime.time >= state.next_light):
        state.trail.point(wrap_index(int(random.randint(0, int((pixels.count - 1)))), len(state.trail)), rgb(40, 8, 0))
        state.next_light = (runtime.time + 0.2)
    pixels.draw(state.trail, transform=Transform(translate=0.0, scale=1.0), overflow="clip")
