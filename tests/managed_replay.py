"""Execute trusted example sources in Python with explicit caller objects.

The compiler never executes sources. This independent test adapter substitutes
PCG32 for Python's generator to compare random draws across native hosts.
"""
import ast
import builtins
import math
from pathlib import Path
from types import SimpleNamespace
from lightvm import rgb, hsv

ROOT = Path(__file__).resolve().parents[1]

def pico_colorwheel(hue):
    hue %= 360
    angle = math.radians((hue % 120) * .75)
    x, y = int(255 * math.cos(angle)), int(255 * math.sin(angle))
    return (x, y, 0) if hue < 120 else (0, x, y) if hue < 240 else (y, 0, x)

def rainbow_colorwheel(hue):
    hue %= 256
    sector = 0 if hue < 85 else 1 if hue < 170 else 2
    rising = min(255, int((hue - 85 * sector) * 3))
    falling = 255 - rising
    return ((falling, rising, 0), (0, falling, rising), (rising, 0, falling))[sector]

def rgb_to_hsv(r, g, b):
    r, g, b = r / 255, g / 255, b / 255
    top, bottom = max(r, g, b), min(r, g, b)
    delta = top - bottom
    hue = 0 if not delta else 60 * (((g-b)/delta) % 6) if top == r else 60 * ((b-r)/delta+2) if top == g else 60 * ((r-g)/delta+4)
    return round(hue), 0 if not top else delta/top, top


class Random:
    def __init__(self): self.seed(1)
    def word(self):
        old = self.state
        self.state = (old * 6364136223846793005 + 1442695040888963407) % 2**64
        x, rotation = (((old >> 18) ^ old) >> 27) % 2**32, old >> 59
        return ((x >> rotation) | (x << ((-rotation) & 31))) % 2**32
    def seed(self, a=None, version=2):
        self.state = 0
        self.word()
        self.state = (self.state + abs(1 if a is None else a)) % 2**64
        self.word()
    def random(self): return (self.word() >> 8) / 2**24
    def randrange(self, *args):
        values = range(*args)
        count = len(values)
        if not count: raise ValueError('empty range')
        limit = 2**32 - 2**32 % count
        while True:
            word = self.word()
            if word < limit: return values[word % count]
    def randint(self, a, b): return self.randrange(a, b + 1)

class Pixels:
    def __init__(self, count): self.count, self.values = count, [0] * count
    def __setitem__(self, at, color):
        assert 0 <= at < self.count and 0 <= color <= 0xffffff
        self.values[at] = color
    def __getitem__(self, at): return self.values[at]
    def fill(self, color, *, start=0, stop=None):
        stop = self.count if stop is None else stop
        assert 0 <= start <= stop <= self.count
        for i in range(start, stop): self[i] = color
    def rgb(self): return b''.join(value.to_bytes(3, 'big') for value in self.values)

class SettingNames(ast.NodeTransformer):
    def visit_Assign(self, node):
        if isinstance(node.value, ast.Call) and getattr(node.value.func, 'id', '') == 'setting':
            target = node.targets[0]
            name = target.attr if isinstance(target, ast.Attribute) else target.id
            node.value.keywords.append(ast.keyword(arg='_name', value=ast.Constant(name)))
        return self.generic_visit(node)

class Example:
    def __init__(self, name, count, settings=None):
        settings = settings or {}
        path = ROOT / 'examples/animations' / f'{name}.py'
        api = SimpleNamespace(profile=lambda _: None, init=lambda f: f, render=lambda f: f, rgb=rgb, hsv=hsv,
            setting=lambda default, _name=None, **bounds: settings.get(_name, default),
            pico_colorwheel=lambda hue: rgb(*pico_colorwheel(hue)), colorwheel=lambda hue: rgb(*rainbow_colorwheel(hue)),
            hue_degrees=lambda c: float(rgb_to_hsv(c >> 16, c >> 8 & 255, c & 255)[0]),
            red=lambda c: c >> 16, green=lambda c: c >> 8 & 255, blue=lambda c: c & 255)
        random = Random()
        def importer(name, *args, **kwargs):
            return api if name == 'lightvm' else random if name == 'random' else builtins.__import__(name, *args, **kwargs)
        namespace = {'__builtins__': {**vars(builtins), '__import__': importer}}
        tree = ast.fix_missing_locations(SettingNames().visit(ast.parse(path.read_text())))
        exec(compile(tree, str(path), 'exec'), namespace)
        self.ctx, self.runtime, self.pixels = SimpleNamespace(), SimpleNamespace(time=0.0, delta=0.0, pixel_count=count), Pixels(count)
        namespace['setup'](self.ctx, self.runtime)
        self.draw = namespace['draw']
        self.frame(0)
    def frame(self, ms):
        self.runtime.delta = ms / 1000
        self.runtime.time += self.runtime.delta
        self.pixels.values = [0] * self.pixels.count
        self.draw(self.ctx, self.runtime, self.pixels)
        return self.pixels.rgb()
