# Audio subscription acceptance record

The implementation follows the [subscription design](design/feature-proposal-audio-subscriptions.md)
and [shared analyser criteria](design/audio-analysis-profiles.md). This record
distinguishes physical checks, simulated transport checks and numerical analysis.
No check played audio through speakers or ran an extended physical stress loop.

The [final validation report](validation/audio-acceptance-2026-10-27.json) records
419 Python tests (three skips), all 33 native contracts, 19 focused transport and
dependency checks, five sample-rate adapter checks, silent browser conformance
and the physical results. The final Pico image matches the installed/tested image
byte for byte. These are local results; a remote GitHub Actions run is not claimed.

| Requirement | Evidence |
| --- | --- |
| One portable implementation; default adaptive percussion and explicit level-rise baseline | Pinned kaj-fi `core/features.c`, `core/onset.c`, `core/profiles.json`; [browser/AudioWorklet report](validation/audio-browser-2026-10-27.json) |
| Identical PCM, both profiles, exact event hops and band edges, bounded calibrated values | [Native/WASM/browser corpus](validation/audio-browser-2026-10-27.json); [physical optimized S3, 0/16/64 bands](validation/audio-s3-parity-2026-10-27.json) |
| Threshold boundaries reported separately | [14 frozen cases](../tests/audio-boundary-cases.json), [native/WASM/physical S3 observations](validation/audio-boundaries-2026-10-27.json): one one-ULP native/WASM disagreement, no S3 disagreement |
| Calibration, 44.1/48/96 kHz, clipping, channel averaging/selection, irregular blocks, sample timestamps, stalls and gaps | [Adapter tests](../web/src/pcm-adapter.test.ts), [browser harness](../tools/check_audio_browser.mjs); every ready hop survives a 500 ms main-thread stall; fixed 1 MiB WASM heap |
| Silent comparison, switching/reset, two algorithms, explicit forwarding, export | [Audio browser check](../tools/check_audio_browser.mjs), browser audio session/feature tests, [source-editor browser check](validation/audio-sources-browser-2026-10-27.json) |
| Same UDP/gateway values and event identities despite loss, duplication, reordering, delay and restart | [Real-socket integration tests](../tests/test_audio_transports.py), [transport report](validation/audio-transports-2026-10-27.json); all 70 values compared through INPUT capture; uint64 source/session/event IDs exceed JavaScript's exact integer range |
| Two receivers on one Sense; layers share a provider | [Physical Sense plus Pico and host](validation/audio-hardware-2026-10-27.json): two leases, three Pico layers using one provider |
| Different sources across A/B, two outputs share one lease, mixed sources in one animation/crossfade, aliases stay local, independent timeouts/reconnect | [Transport integration tests](../tests/test_audio_transports.py): two simulated UDP publishers and two gateway publishers, with distinct services on separate ports |
| Four-provider capacity, generic claims isolated, failed commands/preparations roll back | [Gateway integration tests](../tests/test_audio_gateway.py), [native source contract](../tests/audio_sources_contract.inc), [admission report](validation/audio-manual-admission-2026-10-27.json), mixed fifth-provider rejection |
| Cancellation, hidden retained scenes, reset, pause/resume, power off/on, startup restoration, save/list without subscribing | Gateway and preparation contract tests; [real browser source editor](validation/audio-sources-browser-2026-10-27.json); [physical power cycles/reboot](validation/audio-hardware-2026-10-27.json) |
| Bounded state and unchanged memory reservations | [Pico build accounting](validation/audio-manual-admission-2026-10-27.json), [physical memory/latency](validation/audio-hardware-2026-10-27.json): identical charged heap after three cycles, provider 984 bytes; no FFT in lighting firmware |
| Host service non-MAC identity, restart, suspend/resume, independent ports | kaj-fi `tools/check_subscriptions.py` and `tests/test_diagnostic_subscriber.py`; [loopback report at the diagnostic fix](https://github.com/KajLights/kaj-fi/blob/973491c/docs/validation/audio-loopback-2026-10-27.json) |
| Installed presets are the Home Assistant choices | Preset/config/MQTT tests and [source-editor browser validation](validation/audio-sources-browser-2026-10-27.json); obsolete configured preset lists removed |
| Exact dependency available to clean CI without private-repository credentials | [Pinned source snapshot](../third_party/kaj-audio/README.md), per-file SHA-256 verification, offline extraction and edit-preservation tests |

## Reproduce transport and threshold checks

Use an audio-enabled native host build, the pinned core and the browser WASM
build described in [audio subscriptions](audio-subscriptions.md) and
[browser analysis](audio-analysis-browser.md):

```sh
LIGHTVM_RECEIVER="$PWD/build/host/lightvm" PYTHONPATH=tools:.:tests python -m unittest -v test_audio_transports test_audio_gateway
python tools/check_audio_boundaries.py --core .deps/kaj-fi/core --output build/audio-boundaries-validation.json
```

The UDP peers are explicitly simulations. Physical evidence uses one Sense and
one Pico W. The Sense probe exchanges PCM through USB and compares the actual
optimized FFT implementation; its DSP object-code sections are checked against
the normal application before use. The normal Sense app is restored in a
`finally` block by the physical-run wrapper. The final Pico state is OFF at 40%,
with `audio_spectrum_no_flash` selected; saved configuration, 12 programs and six
presets were preserved. Backups remain outside the repository.

Short physical stack watermarks are observations, not an extended stress
certificate. Different room microphones have no claimed acoustic equivalence.
Floating-point equality is a documented decision boundary: the original corpus
retains exact occurrence/hop acceptance, while the dedicated boundary report
records the one native/WASM disagreement without relaxing those tolerances.
