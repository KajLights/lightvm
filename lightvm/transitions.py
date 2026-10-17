"""Transition drawing helpers, statically inlined like motion and easing.

Progress is normalized to 0..1. Scene reads and complementary RGB blending
lower to bounded native kernels; no Python pixel loop runs on the controller.
"""


def crossfade(pixels, progress):
    pixels.mix(progress)
    return 0


def wipe(pixels, progress, reverse=False):
    boundary = int(progress * pixels.count)
    if reverse:
        boundary = pixels.count - boundary
        pixels.copy_scene("A", stop=boundary)
        pixels.copy_scene("B", start=boundary)
    else:
        pixels.copy_scene("B", stop=boundary)
        pixels.copy_scene("A", start=boundary)
    return 0
