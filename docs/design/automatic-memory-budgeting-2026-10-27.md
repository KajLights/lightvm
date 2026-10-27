# Automatic memory budgeting validation — 27 October 2026

`LIGHTVM_VM_HEAP_BYTES=AUTO` now uses the board's usable RAM after operating
reserves. The implementation and ESP32 capability policy are documented in
[resource budgets](../multicore-rendering.md#resource-profiles). The
[machine-readable results](../benchmarks/automatic-memory-budgeting-2026-10-27.json)
record the firmware hash, linked reservations and short hardware checks.

## Pico W hardware result

The 200-pixel test device now admits **87,160 bytes (85.1 KiB)**, up from
65,536 bytes. The final image uses 134,824 bytes of static SRAM, reserves 24,576
bytes for the two CPU stacks, 7,392 bytes for pixel drivers and 16,384 bytes for
native operation. The automatic limit uses the remaining memory; zero *additional*
unassigned bytes in `ram-report.json` does not mean zero native headroom.

The firmware was installed over USB without erasing the store. Saved configuration
and every preset's size/CRC were unchanged. Two direct Stars → no-flash spectrum
recalls passed, reaching 69,928 bytes of VM use. Switching directly between the
flash and no-flash spectrum presets passed too, reaching **74,582 bytes**. These
peaks exceed the previous ceiling and demonstrate use of the newly admitted RAM.
The strip was muted during the flash-variant checks.

Sampled main/worker stack high-water marks were 5,316/1,012 bytes, within their
16/8 KiB reservations. Wi-Fi, MQTT and the audio receiver remained connected;
the sampled audio status showed no invalid packets or sequence gaps. A subsequent
HTTP query confirmed management remained responsive. These were short functional
checks, not a stress campaign or proof of every native allocation peak.

The device was left running `audio_spectrum_no_flash`, restoring the 40% brightness
found before testing. No effect bytecode or saved preset definitions changed.

## Build and regression checks

- Pico W and Pico 2 W linked RAM and stack gates pass. The Pico 2 W build with
  200-pixel capacity automatically admits **345,064 bytes (337.0 KiB)** while
  retaining 16/8 KiB stacks, 8,448 bytes for drivers and 16 KiB operating headroom.
- A Pico 2 W explicit 262,144-byte ceiling passes. A 524,288-byte request fails
  at link time because it would consume native reserves. The build was restored
  to `AUTO` afterwards.
- ESP32-S3 with Wi-Fi and HTTP builds successfully, including strong native
  memory hooks. Its internal/PSRAM allocation policy is covered with simulated
  capability pools; ESP32 hardware was not flashed for this task.
- All **33** native ASan/UBSan tests, **24** Pico build/accounting Python tests
  and **25** audio/input Python tests pass. New cases cover explicit/automatic
  admission, virtual versus physical accounting, shrinking capacity, integer
  overflow, failed realloc preservation, PSRAM preference and internal reserves.

An automatic budget is still an admission policy over the native allocator.
Fragmentation and transient allocation peaks can fail below the ceiling. Existing
transactional scene preparation remains responsible for retaining the running
scene on failure; this change does not alter scene retention or transitions.
