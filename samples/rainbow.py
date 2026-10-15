"""Bytecode port of rainbow.wat, including its speed and brightness waves.

The original advances a 16-bit hue by 20 units per millisecond: one cycle
every 3.2768 seconds. Carry fractional milliseconds across variable frames.
"""
from lightvm import hsv, step, shade

phase = 0
remainder_ms = 0.0


@step
def update():
    global phase, remainder_ms
    elapsed_ms = remainder_ms + delta * 1000.0
    whole_ms = int(elapsed_ms)
    remainder_ms = elapsed_ms - whole_ms
    phase = (phase + whole_ms * 20) % 65536


@shade
def pixel():
    hue = (index * 65536 // count + phase) % 65536
    wave = ((index * 131072 // count + phase * 3) % 65536) // 256
    if wave > 127:
        wave = 255 - wave
    value = wave * 215 // 127 + 40
    return hsv(hue / 65536.0, 1.0, value / 255.0)
