"""Scalar helpers and authoring markers for LightVM's restricted Python API.

The compiler parses these names without importing or executing guest code.
Desktop calls use ordinary Python values; managed setting bindings are supplied
by the compiler and VM, not by these convenience functions.
"""
import math
RGB = int


def rgb(r: int, g: int, b: int) -> int:
    if any(type(c) is not int or not 0 <= c <= 255 for c in (r, g, b)):
        raise ValueError("RGB channels must be integers in 0..255")
    return (r << 16) | (g << 8) | b


def hsv(h: float, s: float, v: float) -> int:
    if not all(math.isfinite(c) for c in (h, s, v)) or not (0 <= s <= 1 and 0 <= v <= 1):
        raise ValueError("HSV requires finite hue and saturation/value in 0..1")
    hue = int((h % 1.0) * 65536 + 0.5) & 65535
    saturation, value = int(s * 255 + 0.5), int(v * 255 + 0.5)
    sector, fraction = divmod(hue * 6, 65536)
    denominator = 255 * 65535
    low = (value * (255 - saturation) + 127) // 255
    q = value - (value * saturation * fraction + denominator // 2) // denominator
    t = value - (value * saturation * (65535 - fraction) + denominator // 2) // denominator
    channels = ((value, t, low), (q, value, low), (low, value, t),
                (low, q, value), (t, low, value), (value, low, q))[sector]
    return rgb(*channels)


def hash01(index: int, seed: int) -> float:
    """A small deterministic modular polynomial mixer; no random sequence/state."""
    x = (index + seed * 131) % 65521
    x = (x * x * 17 + x * 31 + 101) % 65521
    return x / 65521.0


def profile(name):
    if name not in ("animation/1", "layer/1"): raise ValueError("unsupported profile")


class SpriteBatch:
    """Bounded instances of a user-defined Sprite in the compiled runtime."""
    def __init__(self, sprite, capacity):
        raise RuntimeError("SpriteBatch is provided by the compiled native runtime")


class Sprite:
    """Compile-time definition: init(s, runtime, ...), render(s, runtime, pixels)."""
    def __init__(self, *, width, render, init=None):
        raise RuntimeError("Sprite is provided by the compiled native runtime")


class PixelArray:
    """Managed uint8 image with an explicit rgb8/rgba8/rgbw8/rgbwa8 format."""
    def __init__(self, data, *, format="rgba8", dtype="uint8", readonly=False):
        raise RuntimeError("PixelArray is provided by the compiled native runtime")


class Transform:
    """Geometry description accepted by PixelArray.draw and s.transform."""
    def __init__(self, translate=0.0, scale=1.0, origin=0.0):
        self.translate, self.scale, self.origin = translate, scale, origin


class ColorTransform:
    """Colour description; hue is in degrees and the default is identity."""
    def __init__(self, alpha=1.0, brightness=1.0, tint=0xffffff, hue=0.0, saturation=1.0):
        self.alpha, self.brightness, self.tint = alpha, brightness, tint
        self.hue, self.saturation = hue, saturation


def setting(default, *, minimum=None, maximum=None):
    if minimum is not None and default < minimum or maximum is not None and default > maximum:
        raise ValueError("setting outside bounds")
    return default


def init(function):
    return function


def render(function):
    return function


def red(color): return color >> 16

def green(color): return color >> 8 & 255

def blue(color): return color & 255


def pico_colorwheel(hue):
    if type(hue) is not int or not 0 <= hue <= 360: raise ValueError("hue must be in 0..360")
    hue %= 360
    angle = (hue % 120) * math.pi / 240
    x, y = int(255 * math.cos(angle)), int(255 * math.sin(angle))
    return rgb(x,y,0) if hue < 120 else rgb(0,x,y) if hue < 240 else rgb(y,0,x)


def colorwheel(hue):
    if not math.isfinite(hue) or hue < 0: raise ValueError("hue must be finite and nonnegative")
    hue %= 256
    sector = 0 if hue < 85 else 1 if hue < 170 else 2
    rising = min(255, int((hue - 85 * sector) * 3))
    falling = 255 - rising
    return (rgb(falling,rising,0),rgb(0,falling,rising),rgb(rising,0,falling))[sector]


def hue_degrees(color):
    r, g, b = red(color)/255, green(color)/255, blue(color)/255
    top, bottom = max(r,g,b), min(r,g,b)
    delta = top - bottom
    hue = 0 if not delta else 60 * (((g-b)/delta) % 6) if top == r else 60 * ((b-r)/delta+2) if top == g else 60 * ((r-g)/delta+4)
    return float(round(hue))
