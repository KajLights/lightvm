"""LightVM's original Python authoring SDK and reference pixel semantics.

The compiler recognizes these declarations statically. This module supplies a
reference implementation for running trusted programs on a desktop; it is never
embedded in WASM and compilation never executes user source.
MIT License, copyright 2026 Kaj Wik Siebert.
"""
from dataclasses import dataclass
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


@dataclass
class Context:
    program_us: int
    delta_us: int

    @property
    def time(self):
        return self.program_us / 1_000_000.0

    @property
    def delta(self):
        return self.delta_us / 1_000_000.0


class Parameter:
    def __init__(self, name, default, minimum=None, maximum=None, kind=None):
        self.name, self.value = name, default
        self.minimum, self.maximum = minimum, maximum
        self.kind = kind or ("bool" if type(default) is bool else "float" if type(default) is float else "int")


def parameter(name, default, *, minimum=None, maximum=None, kind=None):
    return Parameter(name, default, minimum, maximum, kind)


class Pixels:
    def __init__(self, name, retention="fresh"):
        if name != "A" or retention not in ("fresh", "retained"):
            raise ValueError("initial profile supports A with fresh or retained pixels")
        self.name, self.retention = name, retention
        self.count = 0
        self.committed, self.presence = [], []
        self.working, self.working_presence = [], []

    def bind(self, count):
        self.count = count
        self.committed, self.presence = [0] * count, [False] * count

    def begin(self):
        self.working = self.committed.copy() if self.retention == "retained" else [0] * self.count
        self.working_presence = self.presence.copy() if self.retention == "retained" else [False] * self.count

    def commit(self):
        self.committed, self.presence = self.working.copy(), self.working_presence.copy()

    def _index(self, index):
        if type(index) is not int or not 0 <= index < self.count:
            raise IndexError(index)

    def __setitem__(self, index, color):
        self._index(index)
        if type(color) is not int or not 0 <= color <= 0xffffff:
            raise ValueError("invalid packed RGB8")
        self.working[index], self.working_presence[index] = color, True

    def __getitem__(self, index):
        self._index(index)
        return self.working[index]

    def present(self, index):
        self._index(index)
        return self.working_presence[index]

    def fill(self, color):
        for i in range(self.count):
            self[i] = color

    def clear(self):
        self.fill(0)

    def erase(self, index):
        self._index(index)
        self.working[index], self.working_presence[index] = 0, False


def pixels(name, *, retention="fresh"):
    return Pixels(name, retention)


def on_start(function):
    function.lightvm_trigger = 0
    return function


def on_frame(function):
    function.lightvm_trigger = 1
    return function
