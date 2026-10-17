# PixelArray and user-defined sprites

The Python frontend parses these declarations without executing Python source.
Programs using them emit `lightvm.program/7`, LBC1/4, execution ABI 4 and drawing
API 2. Wrapped drawing emits schema 8 and drawing API 3. Optimized pixel loops
emit schema 9 and drawing API 4; see [pixel maps](pixel-maps.md). Target-aware compilation
checks the required schema. Existing schema 5/6/7/8 packages remain loadable, including the original stored
one-pixel sprite implementation. New SpriteBatch source uses the general API.

## Images and channels

```python
from lightvm import PixelArray

ART = PixelArray([
     64,  64,  64, 255,
    255, 255, 255, 255,
     64,  64,  64, 255,
], format="rgba8", readonly=True)

# In @init: declare instance-owned mutable images.
ctx.image = PixelArray(3, format="rgba8")
ctx.image.copy(ART)
```

`PixelArray(data, format="rgba8", dtype="uint8", readonly=False)` accepts flat
literal byte lists/tuples or an initialization-time pixel count. Formats are
`rgb8`, `rgba8`, `rgbw8` and `rgbwa8`, in that channel order. Byte lists must
contain a whole number of pixels. A count creates zeroed channels, including
zero alpha where present. Sizes and layouts are fixed. `image.count` and
`len(image)` return pixels, not bytes. Indexed reads return packed RGB; indexed
writes replace RGB, set alpha to 255 where present and clear white.

Readonly literal artwork may be declared at module scope. It is packed in the
pinned program package and read directly there, without per-instance image RAM.
Mutable arrays are declared directly in @init and allocated during admission.
All arrays and scratch canvases count towards the existing resource/heap limits.

| Operation | Contract |
| --- | --- |
| `image.has_channel("w")` | Query this array's format, independently of physical hardware. Valid channel literals are `r`, `g`, `b`, `w`, `a`. |
| `image.set_channel("g", 64, start=0, stop=image.count)` | Replace a byte channel; preserve every other channel. Values are 0–255. An absent channel faults. |
| `image.set_alpha(0.5, start=0, stop=image.count)` | Replace alpha absolutely, using normalized 0–1 opacity. Repeated calls do not compound. An absent alpha channel faults. |
| `image.multiply_alpha(0.5, start=0, stop=image.count)` | Intentionally scale existing alpha, saturating at 255. Factor must be finite and nonnegative. |
| `image.fill(color, alpha=255, start=0, stop=image.count)` | Replace selected pixels with RGB plus byte alpha; clear white. |
| `image.copy(source)` | Copy all channels from an equal-size, equal-format image, including readonly artwork. |
| `image.draw(source, transform=Transform(), color=ColorTransform(), sampling="linear", overflow="clip")` | Source-over compatible image formats; clip or periodically wrap at destination edges. Source and destination must differ. |
| `image.transform(ColorTransform(...), start=0, stop=image.count)` | Apply a destructive bulk colour operation in C. |
| `image.point(index, color, alpha=255)` | Source-over an RGB point. |
| `image.gradient(first, last)` | Opaque RGB gradient with exact endpoints. |
| `image.colorwheel(phase, stride)` | Wrapped 256-unit wheel at phase + index × stride. |
| `image.palette(colors, phase, stride)` | Wrapped interpolation through a nonempty integer palette array. |

Ranges use pixel indices with an exclusive stop; negative or reversed ranges and
out-of-bounds indices fault. Readonly writes are rejected at compile/admission.

Mutable images store straight colour independently of alpha. Thus setting alpha
to zero and restoring it preserves colour, including white. `set_alpha` replaces
an existing alpha mask; use copy plus multiply_alpha to scale the original mask.
Layer output is an internal premultiplied surface: use an owned PixelArray when
colour must survive an alpha-zero round trip.

White is independent of RGB; there is no implicit white extraction. Drawing RGB
onto RGBW contributes zero white, and drawing RGBW onto RGB ignores source white.
The current physical scene compositor remains RGB8. Consequently a scene's
`pixels.has_channel("w")` is false, even when an owned image has white. This SDK
supports RGBW images; it does not add a physical RGBW driver.

## Transform descriptions

`Transform(translate=0.0, scale=1.0, origin=0.0)` maps local coordinate x to
`translate + (x - origin) * scale`. Scale stretches/compresses, and negative scale
reflects; zero scale is an error. Translation and origin use pixel units.
Descriptions may be inline, local bindings, or persistent ctx bindings created in
@init and replaced later. They do not allocate per frame.

`ColorTransform(alpha=1.0, brightness=1.0, tint=rgb(255,255,255), hue=0.0,
saturation=1.0)` rotates RGB hue in degrees, scales HSV saturation, multiplies RGB
by tint and brightness, scales white by brightness, then scales alpha. Alpha is
0–1; brightness/saturation are finite nonnegative multipliers. Byte results clamp.
A hue rotation preserves a gradient's spatial colour variation; it does not turn
a uniform colour into a spatial rainbow.

`draw` samples premultiplied channels before applying colour and source-over.
Linear sampling includes transparent samples outside the source so fractional
movement softens edges. `sampling="nearest"` selects nearest-neighbour instead.
Integer source pixel centres map to integer destination centres. Kernels restrict
drawing to the transformed source footprint and process at most 32 pixels per
VM dispatch. Two-dimensional surfaces and rotation remain future extensions.

`overflow="wrap"` makes destination coordinates periodic. It supports a scaled
source width up to the destination count, including fractional placement and
reflection. See [motion and boundaries](motion.md) for fitting helpers, easing
and seam filtering. Wrapping scans at most 32 destination pixels per dispatch.

## Sprite definitions and batches

See [the Stars example](../examples/layers/stars.py) for a complete animation.

```python
def star_init(s, runtime, position, duration):
    s.position = position
    s.duration = duration


def star_render(s, runtime, pixels):
    if runtime.time >= s.duration:
        s.remove()
        return
    pixels.draw(ART)
    s.transform = Transform(translate=s.position, origin=1.0)
    s.color = ColorTransform(alpha=1.0 - runtime.time / s.duration)


Star = Sprite(width=3, init=star_init, render=star_render)

# In the containing animation's @init:
ctx.stars = SpriteBatch(Star, capacity=20)

# In @render:
ctx.stars.spawn(position=position, duration=duration)
ctx.stars.render(runtime, pixels)
```

A definition has a positive literal width, an optional init function and a render
function. `Definition.width` exposes its literal width, including in declarations.
Declare callbacks before their Sprite definition. Callbacks use ordinary
functions, making the contract suitable for future JS functions and Blocks event
sections. Python classes and the other frontends are not implemented here.

`spawn(...)` accepts the initializer's extra arguments, positionally or by name.
It reserves a free slot, resets its state and transforms, and calls init exactly
once. It returns false when full, without invoking init or replacing an instance.
`batch.active` is the number of occupied slots. Capacity is independent of pixel
count; memory admission sets the practical limit. There is no placement policy or
collision avoidance: overlapping instances are allowed.

`render(runtime, pixels, overflow="clip")` visits active slots in stable order.
Use `overflow="wrap"` for periodic destination drawing. Each sprite gets a
cleared local RGBA canvas of the definition's width, reused after composition.
Its `runtime.time` is seconds since spawn, starting at zero; `runtime.delta` is
seconds since its previous callback (or since spawn for the first callback).
Native integer
microsecond clocks avoid subtracting two large f32 program times. Its
`runtime.pixel_count` and `pixels.count` are the local canvas width.

Sprite fields are independent scalar state (i32, f32, RGB, bool). Create them
unconditionally in the initializer and keep their types fixed across spawn sites.
The current limit is 24 scalar slots, including eight built-in transform values.
At least one source spawn call is needed to infer an initializer's argument/field
types. Instance-owned arrays, nested/recursive batch operations and arbitrary
object allocation inside callbacks are not supported. Shared readonly artwork is
available by module name.

`s.transform` and `s.color` persist per instance and apply once to the completed
local canvas. This preserves group opacity when local drawing operations overlap.
`s.remove()` releases the slot and suppresses its composition immediately; render
may then return without a value. Slots can be reused by subsequent spawn calls.
Nothing automatically ages state fields, expires sprites, moves them or fades
them. Those behaviours are user code.

The compiler lowers callbacks and batch traversal into ordinary bytecode control
flow. The runtime can suspend at any instruction or kernel continuation and
resume the same frame without repeating callbacks or advancing time. PixelArray
kernels and slot scans (eight slots per dispatch) are native. No rendering or
spawning path allocates heap memory. A slow or faulting callback follows the
existing layer scheduling/fault-publication contract.

## Package extension

Header word 80 is 2 for drawing API 2, or 3 when wrapping is used; words 84–92 remain zero. This lets older
runtimes reject the extension before execution. Native call sites retain their
16-byte size and use their formerly reserved trailing four bytes for arguments
five through eight; old calls still have zero padding. Bindings 27–52 implement
the SDK and generic storage. Resource kinds 14–17 are straight images, kind 18 is
generic instances, and kind 19 is reusable transform scratch. Resource flags are
0 for mutable owned storage, 1 for output and 2 for package-backed readonly images.
The verifier independently checks kinds, mutability, stage rights and signatures.
Binding 52 is periodic image drawing and requires API 3; existing bindings retain
their signatures and API 2 compatibility.
