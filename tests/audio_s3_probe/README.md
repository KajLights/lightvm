# Physical S3 fixture check

This diagnostic application drives the pinned kaj-fi core and its actual
`main/fft.c` optimized S3 adapter using PCM received over USB. It has no microphone,
network, NVS or storage writes. A CRC protects every request and response. The
fixture runner compares every ready hop with the native portable FFT, validates
exact band edges/events, and checks gap/reset/silence behavior and DSP duration.

With ESP-IDF **5.5.3** activated and a clean kaj-fi checkout at `kaj-audio.rev`:

```sh
idf.py -C tests/audio_s3_probe -B "$PWD/build/audio-s3" \
  -DKAJ_AUDIO_CORE=/absolute/path/to/kaj-fi/core build
```

Back up the device's configuration and retain its normal application image.
Flash **only** `build/audio-s3/audio_s3_probe.bin` at `0x10000`, preserving the
existing bootloader, partition table and data partitions. Do not use the generated
whole-project flash command. Run:

```sh
python tools/check_audio_s3.py --core /absolute/path/to/kaj-fi/core \
  --port /dev/cu.usbmodem101 --bands 0,16,64 \
  --application-build /absolute/path/to/kaj-fi/build-audio \
  --firmware build/audio-s3/audio_s3_probe.bin --output build/audio-s3-parity.json
```

Restore the normal application at `0x10000` after the check, including on failure,
and verify its configuration and network state. The runner uses the shared
no-reset USB transport: setting DTR/RTS false through ordinary PySerial open can
still transiently reset native USB Serial/JTAG on macOS.

The probe uses the same compiler options as the application. The runner compares
every machine-code section in `features`, `onset` and the S3 FFT adapter against
the normal application build before testing, and records their hashes.

The [27 October run](../../docs/validation/audio-s3-parity-2026-10-27.json) completed
all six corpus cases plus gap/reset checks in 24.40 seconds, without playback.
It used the existing Sense (`1cdbd45cce94`, ESP32-S3 revision 2). Native/S3 event
hops matched exactly, metric error stayed below `2.54e-7`, and the largest measured
DSP hop was 1,963 µs against a 21,333 µs hop budget. This is digital parity evidence;
it is not a microphone acoustic comparison or threshold-boundary qualification.
