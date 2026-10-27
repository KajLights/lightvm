# Make a light blink once per second while the controls stay usable.
from lightvm import profile, hsv, render
from lightvm.motion import pingpong

profile("animation")

@render
def draw(state, runtime, pixels):
    pixels.fill(hsv(0.08, 1.0, (0.2 * pingpong((runtime.time / 2.0)))))
