"""Scalar motion helpers, compiled to ordinary guest bytecode.

Time has no special meaning here: pass seconds / duration for timed progress,
or use the same functions for any other numeric signal.
"""


def repeat(x):
    """Wrap into [0, 1), including negative input."""
    return x % 1.0


def wrap_index(index, count):
    """Keep a whole-number position on a ring, including negative positions."""
    assert count > 0
    return index % count


def wrap(value, length):
    """Repeat a number from zero up to length, including backwards motion."""
    assert length > 0
    return value % length


def pingpong(x):
    """0 -> 1 -> 0; each leg takes one input unit."""
    p = x % 2.0
    if p > 1.0:
        return 2.0 - p
    return p


def clamp01(x):
    return min(1.0, max(0.0, x))


def lerp(first, last, progress):
    """Interpolate without clamping; progress outside [0, 1] extrapolates."""
    return first + (last - first) * progress


def _fit_edge(width, count, scale, origin, end):
    assert width > 0.0 and count > 0.0 and scale != 0.0
    assert width - width == 0.0 and count - count == 0.0
    assert scale - scale == 0.0 and origin - origin == 0.0
    assert width * max(scale, -scale) <= count
    left = (-0.5 - origin) * scale
    right = (width - 0.5 - origin) * scale
    if end:
        return count - 0.5 - max(left, right)
    return -0.5 - min(left, right)


def fit_start(width, count, scale=1.0, origin=0.0):
    """Smallest translation that fits the transformed image in count pixels."""
    return _fit_edge(width, count, scale, origin, False)


def fit_end(width, count, scale=1.0, origin=0.0):
    """Largest translation that fits; equal-size images have one position."""
    return _fit_edge(width, count, scale, origin, True)
