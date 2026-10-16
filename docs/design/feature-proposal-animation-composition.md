# LightVM Animation Composition and Reusable Backgrounds

Status: Deferred; separate alpha design required

Date: 12 October 2026

The earlier retained-paint, binary-presence and erase/clear proposal is withdrawn.
Those features have been removed from the SDK, compiler and control runtime.
Proper alpha transparency and composition will be designed and implemented in
separate work. This note records the intended use cases and remaining decisions;
it does not define a replacement alpha API.

The implemented [control ABI 2](../control-api.md) supplies one RGB8 frame
through deployment-injected `ctx.pixels`. Every guest step begins black. Indexed
RGB writes, fill and own-step readback are supported. Working and committed
frames exist for successful-step publication and failure recovery; completed
pixels are not copied into the next guest step. Scalar simulation state and
program time persist independently. Wipe redraws from scalar progress each frame.

The runtime has one resident production slot A and no scene compositor, alpha
channel or transparency sentinel. Black is ordinary RGB. Identical bytecode can
already render in independent offscreen instances; only native presentation
submits physical output.

## Intended experience

A user should be able to place Twinkle over Color, Rainbow or Pattern, adjust
each animation independently, and change the background while stars continue.
A standalone stars-on-colour preset should reuse the same foreground and Color
background. Each sparse animation should not duplicate a background painter.

Twinkle currently reduces RGB brightness towards black. That produces a useful
single RGB frame, but it does not smoothly reveal another animation underneath.
A future foreground needs proper alpha to fade into the live background. The
alpha design must define that behavior explicitly, including intentional opaque
black and transparent pixels, without guessing transparency from RGB values.

Examples such as Wipe can keep progress in animation state and redraw the
result; they do not require implicit persistent pixel paint. Filters and effects
that need prior simulation data should own explicit state under a documented
future capability rather than treating the final scene as their history.

## Ownership and deployment

The native engine supplies each layer's destination. Programs must not select
arbitrary layer or physical output names. Loading the same source twice produces
independent instances with their own globals, parameters, clocks and frame
buffers. The compositor owns layer order and one physical publication per
completed scene frame. Native brightness and wire ordering apply after the final
composition and any A/B mixing.

The [control model](feature-proposal-control-api.md),
[device model](feature-proposal-device-support.md),
[web editor](feature-proposal-web-editor.md) and
[build profiles](feature-proposal-build-profiles.md) should share that ownership
boundary. [A/B transitions](feature-proposal-ab-transitions.md) mix completed
source frames; per-layer alpha and source-to-source transition weights have
different lifecycles. Combining the features requires a defined order and joint
resource admission.

## Decisions for the separate alpha work

Specify these together before exposing composition:

- The colour/alpha representation, units, defaults and supported precision.
- Straight or premultiplied colour, colour-space rules, blending and rounding.
- Unwritten-pixel initialization, full-frame redraw and opaque/transparent black.
- Layer order, any lower-input readback and the scope of own-step reads.
- Shared frame timing, independent simulation state, pause/resume and removal.
- Working/committed scene publication, traps and native output failure behavior.
- Layer/instance limits, complete memory admission and allocation-free rendering.
- Source metadata, discovery, diagnostics and migration/versioning rules.

These are open design questions, not currently supported options. Keep the alpha
contract independent of third-party animation APIs. All new implementation should
be originally authored under MIT; the
[provenance rules](feature-proposal-control-api.md#prior-art-and-provenance)
continue to exclude GPL easing code and unverified dependency bodies.

## Future verification

The alpha increment should demonstrate smooth Twinkle over a moving Rainbow,
independent parameter updates and clocks, correct opaque/transparent black,
exact blend endpoints, and one completed native output per scene frame. Use
fixed timestamps/seeds for reference versus wasm3 comparisons on both host
profiles, test budget rejection and failure snapshots, and cross-build every
supported board. Physical throughput remains a hardware measurement.
