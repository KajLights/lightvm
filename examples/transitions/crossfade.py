"""Blend scene A into scene B at the supplied progress."""
from lightvm import profile, render
from lightvm.transitions import crossfade

profile("transition")


@render
def draw(ctx, runtime, pixels):
    crossfade(pixels, runtime.progress)
