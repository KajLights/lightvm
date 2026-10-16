# Native composition on Pico W

Measured 16 October 2026, Release build, Pico W, Wi-Fi + HTTP + boot sequence
enabled, Pico SDK 1.5.1, Arm GCC 13.2.1. All four physical outputs have 200 LEDs.
The firmware includes the existing PIO/DMA presenter. Both workloads were
measured under the same firmware, rather than comparing against an older
blocking-output build.

[Machine-readable results](native-composition-pico-w-results.json) include the
comparison, six stability samples and a final firmware smoke test with its UF2
SHA-256. That final test also removed/recreated a physical device while its peers
continued, confirming the cleanup path under real driver service callbacks.

| Output | Pin | Original workload | Original fps | Native scene | Native fps |
| --- | --- | --- | ---: | --- | ---: |
| lights | GP16 | Rainbow, 3.3 s | 17.6 | Rainbow + Stars | 51.1 |
| lights2 | GP17 | Rainbow, 0.5 s | 17.6 | Rainbow + Stars | 51.1 |
| twinkle_warm | GP15 | Twinkle, 8 stars, 1.2 s | 17.2 | Color + Stars, capacity 8 | 51.1 |
| twinkle_cool | GP27 | Twinkle, 40 stars, 3.5 s | 16.0 | Color + Stars, capacity 40 | 50.9 |

These are concurrent output rates, measured from counter differences over five
seconds. The native workload has eight animation instances and four compositors.
The original workload has four animation instances. The new Stars algorithm is
not pixel-identical to old Twinkle: it expires before birth, uses a native
free-position pool and fades with 8-bit alpha. Random births also affect cost.

Across the comparison runs, the original Rainbow render used about 13.4 ms CPU
per completed callback; the native Rainbow used 1.59 ms. The original
high-density Twinkle used roughly 22–24 ms.
The native high-density scene used approximately 0.38 ms for Color, 0.7–0.9 ms
for Stars and 1.26 ms for composition. Native Rainbow executed 15 VM dispatches
per render, versus 2,017 for the original. CPU times include completed callback
slices and exclude cooperative service of other instances; compositor times have
their own completed-pass denominator.

A subsequent 60-second run of four scenes sustained approximately 50–51 fps per
output. VM heap stayed exactly 44,672 bytes, with no layer faults. The original
four-program workload used 14,356 bytes. This implementation trades additional
RGBA frames and independent VM contexts for reuse and lower interpreted drawing
cost. It does not establish a guaranteed 60 fps or a leak-free indefinite run.

The physical isolation check paused `lights/stars`, captured its committed RGBA,
generation, render count and program time, then replaced `lights/bg` with Color.
All captured foreground values remained identical. After resuming Stars its clock
advanced, and the background could be replaced with Rainbow again. Saved hardware
configuration and both original stored program payloads survived firmware update
and testing unchanged. The four runtime scenes were left playing.

Host verification covers alpha endpoints and opaque black, group opacity,
gradients/palettes, independent seeded instances, fault retention, lifecycle and
removal, atomic candidate failure, resource-kind rejection and heap admission.
The C output contract borrows a frame across delayed native transfers and checks
its CRC on every poll/cancel; it verifies immutable fronts, coherent sliced
composition, allocation-free rendering, transfer failure recovery and cancellation
before destruction. The suite also runs under the 175-pixel host resource profile.

To reproduce, compile/store `examples/layers/{color,rainbow,stars}.py` and use
the [scene client API](../composition.md#using-the-client) on four configured
outputs. Compare `scene()["frames"]` over a timed interval. For render CPU, divide
the change in `render_total_us` by the change in `render_count` for each selected
layer. For composition CPU, divide the change in `composite_us` by the change in
`composite_frames`. Sample STATUS `heap` throughout the run. Use the same
firmware, physical configuration and observation interval for the original
programs, and record per-instance settings and concurrent workload.
