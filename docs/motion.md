# Motion, easing and strip boundaries

Motion is ordinary animation code. Scalar helpers compile to the same resumable
bytecode as user functions; they own no clocks, sprite state or native animation
policy. Native drawing applies the positions and colours chosen by that code.

```python
from lightvm.motion import repeat, pingpong, clamp01, lerp, fit_start, fit_end
from lightvm.easing import smoothstep

progress = smoothstep(pingpong(runtime.time / 20.0))
position = lerp(first, last, progress)
```

Sample absolute elapsed time for duration-based motion. Skipped frames then
produce the same position as frequent frames, including across reversals. Sprite
`runtime.time` is seconds since spawn; the layer clock excludes paused time.

## Scalar helpers

| Helper | Contract |
| --- | --- |
| `repeat(x)` | Modulo one, in `[0, 1)`, including negative input. |
| `pingpong(x)` | A triangle wave: 0 at 0, 1 at 1, 0 at 2, repeating in both directions. |
| `clamp01(x)` | Clamp to `[0, 1]`. |
| `lerp(first, last, progress)` | `first + (last - first) * progress`; extrapolates outside `[0, 1]`. |
| `fit_start(width, count, scale=1.0, origin=0.0)` | Minimum translation that fits the transformed image in the destination. |
| `fit_end(width, count, scale=1.0, origin=0.0)` | Maximum fitting translation. |

`lightvm.easing` exports `linear`, `quadratic_in`, `quadratic_out`,
`quadratic_in_out`, `cubic_in`, `cubic_out`, `cubic_in_out` and `smoothstep`.
They clamp input to `[0, 1]` and return normalized progress. Smoothstep has zero
velocity at either end, making it useful for soft reversals. Hue cycles normally
use linear progress so their angular speed stays constant.

Only explicit `from lightvm.motion import ...` and
`from lightvm.easing import ...` imports are supported. Aliases are allowed for
these functions. Library functions accept named arguments and the documented
defaults. Arbitrary Python modules, wildcard imports and private helpers remain
unavailable. The compiler parses the bundled library source without executing
guest code, and qualifies library dependencies independently of guest names.

## Fitting a sprite

The transform is `translate + (x - origin) * scale`. Local pixel centres are
`0..width-1`, so geometric edges are `-0.5` and `width-0.5`. Destination edges
are `-0.5` and `count-0.5`. Fitting accounts for these edges, scale, reflection
and origin, independently of the sampling filter's transparent fringe.

For width 15 and count 200, the default fitting translations are 0 and 185.
For width 3, count 200 and scale 2, they are 0.5 and 194.5. A negative scale
reflects the image and changes the fitting translations accordingly.

An equal-size image has one fitting position. Nonpositive dimensions, zero
scale, nonfinite inputs and a scaled width larger than the destination fail an
assertion. On the desktop that raises `AssertionError`; in bytecode it faults
the current invocation using the existing checked-predicate binding. Plain
`assert condition` is also available in guest code, without a message.

`SpriteDefinition.width` exposes the declared width as a compiler-known integer,
including in resource and setting declarations. The containing layer can pass
fitting translations into `spawn(...)`. Inside a sprite callback,
`runtime.pixel_count` and `pixels.count` refer to its local canvas.

## Bounce, teleport, wrap and exit

| Behavior | Language expression / drawing |
| --- | --- |
| Bounce while fully inside | `lerp(first, last, smoothstep(pingpong(time / duration)))` |
| Teleport from the end of a path to its start | `lerp(first, last, repeat(time / duration))` |
| Move once, then hold | `lerp(first, last, smoothstep(clamp01(time / duration)))` |
| Move off the strip | Unbounded `position = speed * time`, with normal clipped drawing. |
| Cross the strip seam continuously | `position = count * repeat(time / duration)`, with `overflow="wrap"`. |

Drawing accepts `overflow="clip"` (the default) or `overflow="wrap"`:

```python
pixels.draw(image, transform=Transform(translate=position), overflow="wrap")
ctx.sprites.render(runtime, pixels, overflow="wrap")
```

Wrapping makes destination positions periodic with period `pixels.count`. A
sprite crossing the right edge appears partly at each end; negative movement
works the same way. This is separate from resetting its position with `repeat`.
Sampling remains `linear` or `nearest` for image drawing; batches use linear
sampling. Group colour and opacity apply once after filtering at the seam, so
a full-strip image does not acquire a dim or translucent join.

The scaled source width must be no larger than the destination. This bounds
sampling to two periodic contributions per destination pixel, including filter
fringes. Wider wrapped images fault. Each dispatch processes at most 32
destination pixels, and wrapping allocates no frame-time storage.

Clipping never removes a sprite: user code may call `s.remove()` when its own
exit or lifetime condition is satisfied, or let it return later.

## Rainbow sprite example

[`rainbow_sprite.py`](../examples/animations/rainbow_sprite.py) draws 15 local HSV
samples in a guest pixel loop, rotates hue through 360 degrees every 10 seconds,
and moves between fitting edges in 20 seconds per leg. On 200 LEDs it starts at
0, reaches 185 at 20 seconds and returns at 40 seconds. The source adapts to any
destination of at least 15 pixels. No native kernel chooses its colours, motion,
durations or reversal policy.

Compile, store, and activate it on an already configured output:

```sh
python -m lightvm.compiler examples/animations/rainbow_sprite.py -o build/examples/rainbow_sprite.lbc
python tools/client.py --port PORT --store-procedure rainbow_sprite build/examples/rainbow_sprite.lbc
python tools/client.py --port PORT --device lights --activate rainbow_sprite
```

The animation's `color_duration` and `travel_duration` settings default to 10.0 and
20.0 seconds. These frozen settings take effect when the instance is created.

## Compatibility

Scalar helpers use existing instructions and need no new runtime API. Ordinary
All new programs use `animation/1` and schema 10. PixelArray and sprites use drawing API 2.
Wrapped drawing adds native binding 52 and drawing API 3 in header word 80.
Optimized [pixel loops](pixel-maps.md) use drawing API 4.
Discovery advertises schema 10 and `native_drawing_api=4`.
Target-aware compilation rejects targets without schema 10 support.
The loader retains stored schema 5–9 packages and rejects binding 52 in an API 2 package.
