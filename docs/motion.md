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
`quadratic_in_out`, `cubic_in`, `cubic_out`, `cubic_in_out` and `smoothstep`, plus
the `sine_in`/`sine_out`/`sine_in_out`, `circular_in`/`circular_out`/`circular_in_out`
and `exponential_in`/`exponential_out`/`exponential_in_out` families. They clamp
finite input to `[0, 1]` and return normalized progress. The new families use f32;
fixed inputs require `float(...)`. See the [numeric helper contract](numeric-types.md#normalized-easing-helpers)
for endpoint and precision rules. Smoothstep has zero
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
Sampling is `linear` or `nearest` for image drawing and batches; the default is
linear. Group colour and opacity apply once after filtering at the seam, so
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
python tools/client.py --port PORT --store-program rainbow_sprite build/examples/rainbow_sprite.lbc
python tools/client.py --port PORT --output lights --activate rainbow_sprite
```

The animation's `color_duration` and `travel_duration` settings default to 10.0 and
20.0 seconds. These frozen settings take effect when the instance is created.

## Shared Sweep animation

[`sweep.py`](../examples/animations/sweep.py) uses one flat brush sprite for
moving eyes, chase, wipe and trails. It runs directly or as a composition layer. Frozen
settings determine the path and reserve all sprite storage at initialization:

| Setting | Meaning |
| --- | --- |
| `eye_count` | Number of eyes, default 2; use 1 for independently layered eyes. |
| `eye_width` | Brush width, default one tenth of the strip, at least one pixel. |
| `duration` | Seconds per traversal; bounce takes twice this for a round trip. |
| `motion` | 0: loop; 1: bounce (default); 2: move once and hold; 3: repeat an inward wipe. |
| `eased` | Cubic ease in/out, default true. False gives constant travel speed. |
| `phase` | Offset in traversals, default 0. A bounce round trip is 2 units. |
| `reverse` | Reverse path direction, including inward to outward motion. |
| `color`, `rainbow_eyes` | Fixed colour, or distinct eye hues (default true). |
| `retain` | Grow the swept painted range, resetting on each cycle. Motion 2 holds it indefinitely. |
| `trail_seconds` | History window for fading samples, default 0. Ignored with retain. |
| `samples` | Reserved samples per eye, default 1, maximum 128. Use more than 1 for a finite trail. |

Useful initialization settings (other settings retain their defaults):

| Effect | Settings |
| --- | --- |
| Clean cylon | `{"motion": 1, "samples": 1}` |
| Forward chase, then hold | `{"motion": 2, "eye_count": 1, "retain": true, "eased": false}` |
| Inward wipe | `{"motion": 3, "eye_count": 2, "retain": true, "eased": false, "rainbow_eyes": false}` |
| Cylon with a trail | `{"motion": 1, "duration": 2.0, "trail_seconds": 0.6, "samples": 8}` |

Looping and bouncing eyes have evenly spaced phases. Inward eyes alternate
between ends; additional pairs are phased evenly. A single inward eye travels
from one edge to the centre. A zero-length path, including a one-pixel strip,
is valid. Nearest sampling scales the one-pixel brush into a solid eye.

Retained paint is an analytic growing brush, so it needs no history buffer.
Finite trails evaluate the same path at fixed ages spanning `trail_seconds`,
apply `alpha = 1 - age / trail_seconds` and draw oldest to newest. Ages before
animation start draw nothing. Skipped frames do not lose trail samples; repeated
renders at the same time are identical. This resamples a known path, rather than
accumulating and repeatedly rounding 8-bit alpha from previous frames.

`samples` controls temporal/spatial density independently of display frame rate.
Few samples can produce separated ghost eyes, especially with narrow, fast eyes;
increase the count for denser trails. This is a sampled trail, not an exact
per-pixel linear fade. Opacity in overlapping samples follows normal source-over.
Use `samples=1` for clean eyes and retained wipes to minimize RAM.
Each sample also runs a full sprite callback every frame. On the attached
200-LED Pico W alongside three other outputs, two eyes with 24 samples rendered
at about 7 fps; eight samples with 24-pixel eyes and two-second traversals
rendered at about 17 fps. The source comments include that measured recipe.
Choose density with both CPU and memory budgets in mind; these measurements
do not establish a guaranteed output rate.

Storage is bounded by `eye_count * samples`. The current Eye record is 104 bytes
on host and Pico, plus one shared batch header/canvas, shared registers and
drawing state. Four clean eyes add only 312 record bytes over one eye; they share
the same full-strip working/committed frames. Four eyes with 24 samples reserve
9,984 record bytes. Oversized settings fail admission while retaining the
running composition. Rendering allocates no memory.

Measured with this Sweep on the 64-bit host, 200 LEDs, fresh composition per case:

| Eyes | Samples per eye | One batch, tracked heap | Separate layers, tracked heap |
| --- | --- | ---: | ---: |
| 1 | 1 | 6,826 | 6,826 |
| 4 | 1 | 7,138 | 21,232 |
| 4 | 24 | 16,706 | 30,800 |
| 8 | 1 | 7,554 | 40,440 |

These bytes include composition frames and animation contexts; native driver/SDK memory
is separate. Pico context sizes differ; these are host measurements, not controller
measurements. The 104-byte sample records have the same size on Cortex-M0+.

For different blend modes, use the same program with `eye_count=1` in separate
layers; [`sweep-layers.json`](../examples/compositions/sweep-layers.json) demonstrates
screen and additive eyes over a fixed background. See [composition](composition.md)
for the additional full-strip frames and independent context costs.
That composition uses 24-pixel eyes and therefore requires at least 24 output pixels.

## Compatibility

Motion helpers, images, sprites, wrapped drawing and pixel maps are part of the
LightVM 1.0.0 baseline. They use the same application release contract as ordinary
animation instructions. There are no drawing API revisions or alternate runtime
paths. Target-aware compilation checks `version` against the program's `requires`.
See [versioning](versioning.md).
