"""Normalized curves, independently derived from their mathematical equations.

MIT License, copyright 2026 Kaj Wik Siebert. No easing-library source is used.
Compiled helpers use f32 intermediates, clamp finite inputs to [0, 1], and
return exact endpoints. Fixed values require an explicit float conversion.
"""
import math


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


def sine_in(t: float) -> float:
    t = linear(t)
    if t == 0.0 or t == 1.0:
        return t
    # 1-cos(pi*t/2), written without cancellation near zero.
    s = math.sin(t * 0.7853981633974483)
    return 2.0 * s * s


def sine_out(t: float) -> float:
    t = linear(t)
    if t == 0.0 or t == 1.0:
        return t
    return math.sin(t * 1.5707963267948966)


def sine_in_out(t: float) -> float:
    t = linear(t)
    if t == 0.0 or t == 1.0:
        return t
    s = math.sin(t * 1.5707963267948966)
    return s * s


def circular_in(t: float) -> float:
    t = linear(t)
    # Rationalized 1-sqrt(1-t*t) retains small positive values.
    square = t * t
    return square / (1.0 + math.sqrt(1.0 - square))


def circular_out(t: float) -> float:
    t = linear(t)
    return math.sqrt(t * (2.0 - t))


def circular_in_out(t: float) -> float:
    t = linear(t)
    if t < 0.5:
        x = 2.0 * t
        square = x * x
        return 0.5 * square / (1.0 + math.sqrt(1.0 - square))
    x = 2.0 * t - 1.0
    return 0.5 + 0.5 * math.sqrt(x * (2.0 - x))


def exponential_in(t: float) -> float:
    t = linear(t)
    if t == 0.0 or t == 1.0:
        return t
    return math.exp((t - 1.0) * 6.931471805599453)


def exponential_out(t: float) -> float:
    t = linear(t)
    if t == 0.0 or t == 1.0:
        return t
    return 1.0 - math.exp(-t * 6.931471805599453)


def exponential_in_out(t: float) -> float:
    t = linear(t)
    if t == 0.0 or t == 1.0:
        return t
    if t < 0.5:
        return 0.5 * math.exp((2.0 * t - 1.0) * 6.931471805599453)
    return 1.0 - 0.5 * math.exp((1.0 - 2.0 * t) * 6.931471805599453)
