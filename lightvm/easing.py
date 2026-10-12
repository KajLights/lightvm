"""Original normalized polynomial curves, derived directly from their equations.

MIT License, copyright 2026 Kaj Wik Siebert. No easing-library source is used.
"""


def linear(t: float) -> float:
    return min(1.0, max(0.0, t))


def quadratic_in(t: float) -> float:
    t = linear(t)
    return t * t


def quadratic_out(t: float) -> float:
    t = linear(t)
    return 1.0 - (1.0 - t) * (1.0 - t)


def quadratic_in_out(t: float) -> float:
    t = linear(t)
    if t < 0.5:
        return 2.0 * t * t
    return 1.0 - 2.0 * (1.0 - t) * (1.0 - t)


def cubic_in(t: float) -> float:
    t = linear(t)
    return t * t * t


def cubic_out(t: float) -> float:
    t = linear(t)
    return 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t)


def cubic_in_out(t: float) -> float:
    t = linear(t)
    if t < 0.5:
        return 4.0 * t * t * t
    return 1.0 - 4.0 * (1.0 - t) * (1.0 - t) * (1.0 - t)


def smoothstep(t: float) -> float:
    t = linear(t)
    return t * t * (3.0 - 2.0 * t)
