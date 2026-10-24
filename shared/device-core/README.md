# Shared device core

LightVM and kaj-fi compile these same native C sources. The library contains
bounded JSON parsing, framed management transport, HTTP sessions and setup,
optional WebSocket transport, Wi-Fi policy, DNS-SD discovery, configuration
revision checks and atomic object persistence. It does not link an interpreter,
LED driver, FFT, microphone driver or audio multicast publisher.

## Integration

Create a `device_config` INTERFACE target, set `DEVICE_WIFI`, `DEVICE_HTTP` and
`DEVICE_WEBSOCKET`, and add this directory with `add_subdirectory`. Enable HTTP
sources with `DEVICE_BUILD_HTTP`; WebSocket requires `DEVICE_BUILD_WEBSOCKET`.
`DEVICE_HTTP_CLIENTS` and `DEVICE_HTTP_RESPONSE_BYTES` size static queues/buffers.
Defaults are in `include/device/config.h`. API revision is in `API_VERSION`.

An application supplies `DeviceHost` identity, capabilities and management
callbacks, the `device/platform.h` allocator/time/storage hooks, and the store
schema hooks. Store type IDs are bounded by `STORE_TYPE_LIMIT`; configuration
and network IDs are shared, other IDs belong to the application. Each type has
an in-memory mutation revision. Persisted config/network revisions are separate.
LightVM supplies its animation/output policies in `src/store_policy.c` and
its host callbacks in `src/device_host.c`. Kaj-fi supplies audio configuration.

All portable service state and application callbacks run on one owner. Native
Wi-Fi callbacks queue bounded events; HTTP sender tasks never call application
code. Applications must hand work to a DSP/render owner at a safe boundary.
`device_checkpoint()` lets cooperative applications service bounded work during
storage. It must not recursively mutate the same store.

`ports/esp32/network_port.c` is the shared ESP-IDF 5.5.3 radio, HTTP and mDNS
adapter. Call `device_esp32_init(host, low_latency)` once, then poll it and
`network_settings_tick()` on the management owner. The explicit latency policy
selects no Wi-Fi power saving for the audio publisher. Host/Pico native adapters
remain in LightVM and consume the same portable services.

## Configuration and persistence

`NetworkSettings` loads, validates revisions, saves and defers reconnection until
a reply has been delivered (or its bounded deadline expires). The shared JSON
network schema is `lightvm.network`; passwords are omitted from public reads.
Saved settings can hold eight networks, hidden-network flags, country, station
or AP mode, HTTP and optional MQTT configuration. Applications reject services
they do not implement. Blank devices can provision through the plain HTML setup
form; the application chooses recovery policy.

`settings_revision_matches()` is shared by both application configuration APIs.
An apply is distinct from a save. The store writes data and catalogue before
publishing an inactive root with a final commit marker; the preceding committed
state remains readable on interrupted writes. Tests exercise torn writes, CRCs,
immutable pinned entries, allocation failure and remount behavior.

Application configuration schemas remain application-specific: LightVM controls
outputs, while kaj-fi controls analysis. Network format, storage transactions and
transport machinery are shared. Experimental incompatible formats should be
wiped/reprovisioned instead of introducing migrations.

## Identity and discovery

The common service remains `_lightvm._tcp.local`, with `lightvm.local` as a shared
bootstrap address. Names are product-specific (`lightvm-xxxxxx`, `kaj-fi-xxxxxx`).
TXT fields are `id`, `board`, `version` (SemVer), `kind`, and decimal `caps`.
Capabilities are configuration=1, light output=2, audio analysis=4, audio
publisher=8. LightVM advertises 3 and `light_controller`; kaj-fi advertises 13
and `audio_analyser`. All fields must validate before a peer is exposed.

`GET /api/v1/controllers` returns the bounded `lightvm.discovery` list, including
kind, version and capabilities. Any peer can answer discovery. A consumer selects
peers by capability before opening its application controls; the LightVM editor
lists only light-output devices. Older experimental advertisements without kind
and capabilities must be reflashed. The cache is limited to eight devices and
expires incomplete/stale peers. Discovery does not carry audio frames.

The management API retains LVM1 framing, `/api/v1/sessions`, `/api/v1/command` and
`X-LightVM-Session`. `python/device_client.py` supplies shared framing and USB/HTTP
transports for both command-line tools. No transport automatically retries a
mutation after an ambiguous response. Native ESP32 USB connections in explicit
no-reset mode leave modem lines untouched and disable POSIX hangup-on-close.

## Validation

LightVM host transport, network, storage, recovery and configuration tests cover
these services. Kaj-fi host tests compile the JSON parser directly without any
LightVM include path; its firmware build links this library without `lightvm_core`.
ESP32-S3 builds cover both applications. Pico W builds include this target in the
RAM and call-graph stack audits, including the application callback adapters.

See [the extraction validation record](VALIDATION.md) for tested profiles,
hardware measurements and the known baseline regression-test failure.
