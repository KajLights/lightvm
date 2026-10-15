"""Run the supplied original methods with deterministic host adapters.

Only Twinkle allocation is replaced: retain its original draw/draw_stars methods,
including spawn-before-expiry and channel math. A positive clock origin avoids
its old zero sentinel while the VM tests separately exercise zero-time stamps.
"""
import ast
from array import array
from fractions import Fraction
import math
from pathlib import Path
from types import SimpleNamespace

ORIGINAL = Path(__file__).with_name('original_picolights')


class Random:
    def __init__(self, seed=1, stream=0):
        self.state = (seed ^ (stream * 0x9e3779b9)) & 0xffffffff or 0x6d2b79f5
        self.draws = 0

    def word(self):
        x = self.state
        x ^= (x << 13) & 0xffffffff
        x ^= x >> 17
        x ^= (x << 5) & 0xffffffff
        self.state = x & 0xffffffff
        self.draws += 1
        return self.state

    def random(self):
        return (self.word() >> 8) / 2**24

    def try_index(self, n):
        limit = 2**32 - 2**32 % n
        for _ in range(4):
            value = self.word()
            if value < limit:
                return value % n
        return None

    def randint(self, lo, hi):
        result = self.try_index(hi - lo + 1)
        if result is None:
            raise RuntimeError('bounded RNG exhausted during initialization')
        return lo + result


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


class Pixels(list):
    def __init__(self, count): super().__init__([(0, 0, 0)] * count)
    def fill(self, color): self[:] = [color] * len(self)
    def rgb(self): return bytes(int(channel) for color in self for channel in color)


class PicoAnimation:
    def __init__(self, pixels, **kwargs):
        self.pixel_object = pixels
        self.draw_count = 0
        self.color = kwargs.get('color', (255, 64, 0))
        self.cycle_complete = False


class TimedAnimation(PicoAnimation):
    def __init__(self, pixels, duration=1, **kwargs):
        super().__init__(pixels, **kwargs)
        self.duration = duration
        self.progress = 0


class SineEaseOut:
    def __init__(self, start=1, end=0, duration=1): self.start, self.end, self.duration = start, end, duration
    def __call__(self, elapsed):
        x = math.sin(elapsed / self.duration * math.pi / 2)
        return self.end * x + self.start * (1-x)


class Original:
    def __init__(self, name, count, settings=None):
        self.name, self.pixels = name, Pixels(count)
        self.settings = settings or {}
        self.random = Random(self.settings.get('seed', 1))
        self.clock = Fraction(1)
        self.time_us, self.credit = 0, 0
        self.period = self.settings.get('update_us', 20_000)
        self.completed = False
        self.cycles = 0

        def to_color(value):
            if value == 'random': return pico_colorwheel(self.random.randint(0, 360))
            if isinstance(value, str):
                value = int(value.removeprefix('#'), 16)
            if isinstance(value, int): return value >> 16, value >> 8 & 255, value & 255
            return value

        namespace = dict(PicoAnimation=PicoAnimation, Animation=PicoAnimation,
                         TimedAnimation=TimedAnimation, SineEaseOut=SineEaseOut,
                         array=array, random=self.random, time=SimpleNamespace(monotonic=lambda: self.clock),
                         to_color=to_color, to_hex=lambda c: "#%02x%02x%02x" % c, random_color=lambda: to_color('random'),
                         rgb_to_hsv=rgb_to_hsv, rainbowio_colorwheel=rainbow_colorwheel, MAX_HUE=255)
        tree = ast.parse((ORIGINAL / (name + '.py')).read_text())
        tree.body = [n for n in tree.body if isinstance(n, (ast.ClassDef, ast.FunctionDef))]
        exec(compile(tree, str(ORIGINAL / (name + '.py')), 'exec'), namespace)
        s = self.settings
        if name == 'color':
            self.effect = namespace['Color'](self.pixels, 'random' if s.get('random_color', True) else s.get('color', 0xff4000))
        elif name == 'pattern':
            pattern = None if s.get('random_pattern', True) else '#ff0000,#00ff00,#0000ff'
            self.effect = namespace['Pattern'](self.pixels, pattern=pattern, to_fit=s.get('to_fit', True))
        elif name == 'rainbow':
            self.effect = namespace['Rainbow'](self.pixels, color='random' if s.get('random_color', True) else s.get('color', 0xff0000), duration=s.get('duration', 60), scale=s.get('scale', 1))
        elif name == 'chase':
            self.effect = namespace['Chase'](self.pixels)
            self.effect.color = to_color(s.get('color', 0xff4000))
        elif name == 'wipe':
            self.effect = namespace['Wipe'](self.pixels, step=s.get('step_width', 1), color=to_color(s.get('color', 0xff4000)))
        else:
            self.effect = namespace['Twinkle'](self.pixels,
                color='random' if s.get('random_background', True) else s.get('color', 0xff4000),
                brightness=s.get('brightness', .05), twinkle_color=None if s.get('random_stars', True) else to_color(s.get('twinkle_color', 0xffffff)),
                duration=s.get('duration', 2), probability=s.get('probability', 2), max_stars=s.get('max_stars'))
            effect = self.effect
            # Accepted timestamp correction: exact logical births, not f32 absolute time.
            effect.start_birth_time = [0] * len(effect.start_birth_time)
            def bounded_add_star():
                slot = next((i for i, birth in enumerate(effect.start_birth_time) if not birth), None)
                if slot is None: return
                occupied = {effect.star_positions[i] for i,birth in enumerate(effect.start_birth_time) if birth}
                free = [i for i in range(count) if i not in occupied]
                if not free: return
                rank = self.random.try_index(len(free))
                if rank is None: return
                if effect.twinkle_color is None:
                    hue = self.random.try_index(361)
                    if hue is None: return
                    color = pico_colorwheel(hue)
                else: color = effect.twinkle_color
                effect.start_birth_time[slot] = self.clock
                effect.star_positions[slot] = free[rank]
                effect.star_colors[slot*3:slot*3+3] = array('B', color)
            effect.add_star = bounded_add_star
        self.tick()

    def tick(self):
        self.clock = Fraction(1_000_000 + self.time_us, 1_000_000)
        if self.name == 'rainbow':
            self.effect.progress = (self.time_us / 1_000_000 % self.effect.duration) / self.effect.duration
        self.effect.draw_count += 1
        self.effect.draw()
        if self.name == 'wipe' and self.effect.cycle_complete:
            self.completed = True

    def frame(self, ms):
        if self.completed: return self.pixels.rgb()
        if self.name in ('color', 'pattern'):
            self.time_us += ms * 1000
            self.effect.draw()
        else:
            ticks, self.credit = divmod(self.credit + ms * 1000, self.period)
            for _ in range(ticks):
                self.time_us += self.period
                self.tick()
                if self.completed: break
        return self.pixels.rgb()
