"""Reveal scene B across scene A, optionally from the opposite end."""
from lightvm import profile, init, render, setting
from lightvm.transitions import wipe

profile("transition")


@init
def setup(ctx, runtime):
    ctx.reverse = setting(False)


@render
def draw(ctx, runtime, pixels):
    wipe(pixels, runtime.progress, reverse=ctx.reverse)
