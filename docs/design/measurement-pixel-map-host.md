# Generic pixel-map performance on the host

Measured 16 October 2026, Darwin arm64, Apple Clang Release build, 200 output
pixels. All variants ran through the same host executable, scene compositor and
command protocol. [Raw results](pixel-map-host-results.json) include executable
and source hashes and all timing windows.

| Workload | Ordinary loop, µs/frame | Bulk map, µs/frame | Speedup |
| --- | ---: | ---: | ---: |
| Full-strip colourwheel | 232.757 | 8.580 | 27.1× |
| Nonlinear HSV function | 298.810 | 11.947 | 25.0× |
| 15-pixel rainbow sprite, full callback | 72.143 | 52.377 | 1.38× |

The previous specialized `pixels.colorwheel` fill measured 5.743 µs/frame. The
generic full-strip map therefore takes about 1.49× its CPU time. Its bytecode is
879 bytes versus 903 for the ordinary loop and 767 for the specialized fill.
VM dispatches fall from 1,813 to 18 per full-strip rainbow frame; the specialized
fill uses 15. The nonlinear function drops from 2,807 to 12. Sprite callbacks
drop from 236 to 85 dispatches; batch traversal and geometry/colour composition
still dominate much of that callback.

The layer rainbow source now contains its own pixel loop. The optimizer sees
typed arithmetic and colour operations, with no rainbow-specific recognizer.
The nonlinear comparison and sprite use HSV construction through the same bulk
backend. The specialized fill remains only as a compatibility operation and a
benchmark/reference implementation.

Each figure is the median of seven windows of 300 frames. Each window gets a
fresh instance and ten warm-up frames. Variant order reverses on alternate
rounds. Timing is the difference in engine `render_total_us` divided by the
difference in completed renders, excluding Python transport and snapshot delay.
Times include the engine's normal render bookkeeping and instrumentation.

Native fill, ordinary loop and optimized map produce identical RGBA at the four
tested timestamps for the full-strip rainbow. Ordinary and optimized versions
also agree exactly for the nonlinear function and sprite. The test suite covers
more timestamps, helper inlining, negative arithmetic, image formats, chunk
boundaries, empty ranges, fallback semantics, faults and forged bytecode.

Heap remained constant within every window. The full-strip ordinary and bulk
programs both use 6,308 bytes of scene/VM heap, versus 6,274 for specialized fill;
the sprite uses 6,714 bytes with either execution mode. Bulk scratch is fixed C
stack storage rather than extra per-instance heap.

These are host CPU measurements, not Pico measurements or physical LED frame
rates. They should not be extrapolated to the Pico's soft-float implementation.

Subsequently, a complete cached Arm toolchain was located and the Pico W firmware
built successfully, including its RAM budget check. The optimized 15-pixel sprite
was deployed on GP16 with 200 pixels, a 10-second colour cycle and a 20-second
traversal in each direction. A roughly three-second device sample recorded 142
completed renders, about 47 output frames per second and 3.87 ms of render CPU
time per frame, with the other three outputs active and no animation faults.
This launch check was not a scalar-versus-bulk Pico benchmark.
