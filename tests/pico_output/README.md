# Pico output verification

The optional `output_probe` image tests the production PIO/DMA driver with on-chip
GPIO capture, all RGB orders, brightness endpoints, busy/count rejection,
close/reopen, DMA exhaustion, stalled-transfer cancellation/retry and late polling.
It also reports submission and wall time for one, two and four outputs.
No alternate output driver is included.

Build the normal Pico project with `-DLIGHTVM_OUTPUT_PROBE=ON`, using the same SDK,
board and feature flags as the firmware under test. This also builds normal
`lightvm.uf2`. The probe produces `output_probe.uf2` and does not open the store.

Flashing and running the probe requires an explicit hardware test request.
Preserve configuration and flash data, retain a current rollback image, and use
a flashing operation that preserves the reserved store and boot journal.
After manually flashing the probe, read results with:

```sh
python tools/read_output_probe.py --port PORT --output build/output-probe.json
```

Restore the normal firmware afterward. Capture verifies digital output on the
chip; throughput and strip electrical behavior require separate measurements.
No results from previous package contracts are presented as current evidence.

`tools/check_output_lifecycle.py --port PORT --held OUTPUT --peer OUTPUT` checks
paused-frame isolation during peer reset and synchronous FRAME readback using
the current application protocol. It requires two configured running programs,
restores their pause flags and checks persistent metadata. The peer's transient
state and clocks restart. Use only when explicitly testing that setup.
