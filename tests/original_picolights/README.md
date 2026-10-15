# Original algorithm fixtures

These six files are snapshots of Kaj's supplied picolights animation modules.
Snapshot: picolights HEAD `f6480b4845b2ba3af4fbf0ffb1f398d362dda772`, 15 October 2026.
They are reference material, not compiler input. The replay harness executes the
original class methods with controlled clocks, pixel bindings and random draws.
It substitutes the explicitly approved bounded allocation correction for
Twinkle's `add_star`, and supplies the missing colour binding for Chase.

The harness implements the sine-out equation independently; it does not vendor
the GPL-marked easing implementation. Rainbow's colourwheel compatibility follows
[CircuitPython's rainbowio definition](https://github.com/adafruit/circuitpython/blob/main/shared-module/rainbowio/__init__.c).
