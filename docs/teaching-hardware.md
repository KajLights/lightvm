# Teaching hardware acceptance

The software checks exercise the course on the native host and browser VM.
They do not establish an optical refresh rate, device command latency or beginner
comprehension. This file defines the remaining device run.

The attached Pico W was identified through a read-only, no-reset query on
27 October 2026. It was running a 200-pixel audio installation with active
compositions. Its configuration, programs and playback were not replaced for
this review. The teaching firmware has not been flashed onto it. Testing that
installation's existing image would not validate the changed runtime diagnostics.

Device testing is deferred: the controller is in use by another job. Run this
campaign only after the controller becomes available for testing.

## Run when a test controller is available

1. Export its configuration, network settings, presets and editable source first.
   Record the firmware SHA-256, source revision plus diff, board, compiler, clock,
   worker count, output driver, brightness, configured count and all memory limits.
   Install the teaching build using the project's normal board instructions.
2. Use the solutions and extensions from lessons 3, 4, 6–12. Compile each with
   `optimize=False` and `optimize=True`. Freeze the packages before measuring.
   Compare pixels and state at the same seed, settings, inputs and timestamps.
   Reset the stopped program before replay so preparation/autoplay time cannot
   introduce an unequal starting clock.
3. Test 25, 50 and 100 pixels, plus 1, 2, 12, 16, 24, 31, 32, 33, 63, 64 and 65
   for boundaries. For a physically shorter strip, larger logical counts measure
   execution only. Test a real ring seam in both directions.
4. Run at zero delta, 16, 33, 250 and 1,000 ms, exact period boundaries, a long
   program clock, progress 0/1, a fully occupied search array and repeatable seeds.
   Test the deliberate bad index and endless loop. Check the source location,
   prior complete display, stop/reset and continued controller command service.
5. Warm up, then record at least five 30-second windows with variant order
   alternated. Report p50/p95/p99 frame intervals, **distinct presentations**,
   render CPU, elapsed render time, command latency, prepare/first-frame latency,
   package bytes, map-plan bytes, heap/peak and main/worker stack peaks.
   A reused static picture is not a newly calculated frame.
6. Repeat with one and two workers on boards supporting that choice. Use
   [the existing performance tools](../tools/benchmark_pico_campaign.py),
   [numeric replay tool](../tools/benchmark_numeric.py) and
   [measurement protocol](optimisation-protocol.md) for capture and restoration.
   Use the published lesson sources for the teaching run rather than substituting
   a synthetic shader. Restore the original saved setup after the run.

Provisional classroom target: a core example and an ordinary extension present
60 fresh frames/s at 100 pixels on the selected baseline controller, with p95
render CPU below 8.3 ms and usable controls throughout. Record any output-driver
limit and actual command/first-frame distributions. These are acceptance targets,
not results achieved by the current host measurements.

## Decisions that need this data

Do not adopt a universal small-count map threshold from desktop measurements.
Host dispatch and target arithmetic costs differ. Keep any target threshold only
if repeated measurements improve latency or CPU without increasing jitter,
breaking lazy faults or changing memory admission. Likewise, add a guarded map
for skipped writes or a native JavaScript remainder operation only when a real
course workload demonstrates a useful gain. Current ordered execution already
preserves their correct behaviour.

Physical Pico W, RP2350 and ESP32 qualification remains open. Software build and
stack-accounting results are recorded separately in
[the implementation record](design/teaching-implementation.md).
