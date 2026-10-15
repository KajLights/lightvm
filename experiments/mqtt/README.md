# Separate MQTT WASM experiment

This experiment is disabled by default. `LIGHTVM_MQTT_EXPERIMENT=ON` adds an
engine-owned service alongside slot A; MQTT packets, the asynchronous state
machine, HA topics, discovery and JSON all live in the separately uploaded guest.
The native experiment supplies nonblocking IPv4 TCP and validated parameter
transactions. This is a technology test, with no TLS, credentials, Matter, OTA or
complete light entity model. Use an isolated broker.

Read [the measured report](REPORT.md) before drawing a capacity conclusion.
Successful Wasmtime protocol tests are not evidence that wasm3 fits a device.

## Build the identical artifact

Requirements: Python 3.12, `requirements.txt`, Zig **0.13.0**, and optionally
`paho-mqtt==2.1.0` for the broker/traffic runners. Install temporary tools outside
existing build directories, for example:

```sh
python -m pip install --target /tmp/lightvm-mqtt-tools ziglang==0.13.0 paho-mqtt==2.1.0
python experiments/mqtt/build.py --zig /tmp/lightvm-mqtt-tools/ziglang/zig
```

The freestanding C build links only the pinned coreMQTT **v2.3.1 serializer**,
`service.c` and a minimal memory/string implementation. Unused serializer code
is eliminated. `vendor/coreMQTT/SOURCE.json` records and verifies unmodified
upstream source hashes and the archive hash; its MIT license is in
`licenses/coreMQTT-v2.3.1.txt`. Zig's linker receives hexadecimal sizes because
its 0.13.0 compiler driver parses these forwarded options as hexadecimal.
The output has **one standard 64 KiB page**, a 4 KiB C stack, and no WASI imports.
No board-specific constants enter the artifact.

```sh
cmake -S . -B build/mqtt-host -DLIGHTVM_HTTP=ON \
  -DLIGHTVM_MQTT_EXPERIMENT=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/mqtt-host --parallel 4
ctest --test-dir build/mqtt-host --output-on-failure
LIGHTVM_RECEIVER="$PWD/build/mqtt-host/lightvm" python -m unittest discover -s tests -v
```

The MQTT tests use the same artifact in Wasmtime with deterministic fragmented
I/O, partial writes, a simulated clock and acknowledgement control. Small
one-page firmware fixtures exercise the real wasm3 deadline, memory bounds,
TCP cleanup, generation checks and independent HTTP replacement under the
unchanged host VM budget. The full MQTT artifact's failed firmware admission is
also asserted. These fixtures do not replace the workload in capacity results.

Activate ESP-IDF **5.5.3**, then build each XIAO in its own directory:

```sh
for chip in c3 s3 c6; do
  idf.py -C platforms/esp32 -B "$PWD/build/mqtt-$chip" \
    -D LIGHTVM_BOARD=xiao_esp32$chip -D LIGHTVM_WIFI=ON -D LIGHTVM_HTTP=ON \
    -D LIGHTVM_MQTT_EXPERIMENT=ON build
done
```

For the C3 measurement build, also set `-D LIGHTVM_BOOT_SEQUENCE=ON` to preserve
the connected board's existing service profile. Keep its VM ceiling at 128 KiB.
With Pico SDK **1.5.1** and Arm GNU **13.2.Rel1**:

```sh
cmake -S . -B build/mqtt-pico-w -DLIGHTVM_PLATFORM=pico -DPICO_BOARD=pico_w \
  -DLIGHTVM_WIFI=ON -DLIGHTVM_HTTP=OFF -DLIGHTVM_MQTT_EXPERIMENT=ON \
  -DPICO_SDK_PATH=/path/to/pico-sdk-1.5.1 -DCMAKE_BUILD_TYPE=Release
cmake --build build/mqtt-pico-w --parallel 4
```

The Pico experiment requires Wi-Fi and rejects HTTP. Only this explicit profile
reserves **112 KiB** for the VM. `check_pico_ram.py` retains its **16 KiB** operating
headroom, 16 KiB stack and 3,072-byte maximum driver reservation. Ordinary Pico W
Wi-Fi builds keep their existing 64 KiB VM profile. On CMake 4, preconfigure the
build-local `pioasm` with `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` if the old SDK's
external project needs that compatibility option; do not edit the SDK checkout.

## Experimental ABI

Import namespace: `lightvm.experimental.v1`. Guest exports:
`service_init(ipv4: i32, port: i32) -> void` and `service_step() -> i32`.
The state return is diagnostic: 0=backoff, 1=TCP connecting, 2=CONNACK pending,
3=SUBACK pending, 4=MQTT connected. Native export initialization and all guest
steps share the **2 ms cooperative deadline** and run on the animation engine's
owner. A fault stops the service and closes its connections; slot A and management
remain available. Cooperative checks occur at calls and loop iterations, so
this is not a hard real-time preemption guarantee.

| Import | Signature | Meaning |
|---|---|---|
| `tcp_connect` | `(i32 ipv4, i32 port) -> i32` | IPv4 in network byte order as a numeric integer, e.g. `0x7f000001`; positive generation handle or -1 |
| `tcp_state` | `(i32 handle) -> i32` | 0 pending, 1 connected, -1 invalid/closed/error |
| `tcp_read`, `tcp_write` | `(i32 handle, i32 offset, i32 size) -> i32` | Positive partial byte count, 0 backpressure, -1 invalid/closed; maximum 2,048 bytes |
| `tcp_close` | `(i32 handle) -> i32` | 0 success, -1 stale handle |
| `time_ms` | `() -> i64` | Monotonic milliseconds |
| `identity` | `(i32 offset, i32 capacity) -> i32` | Stable native device ID plus NUL; count excluding NUL, -1 insufficient capacity |
| `animation_state` | `(i32 offset, i32 size) -> i32` | Exactly 40 bytes, described below |
| `speed_set` | `(f64 speed, i32 generation, i32 revision) -> i32` | Queue one transaction for validation after guest return; 0 queued, -2 occupied |

`animation_state` contains little-endian u32 generation and parameter revision,
f64 speed, then u32 frames, tracked VM bytes, last render microseconds, previous
transaction result (0 none, 1 accepted, 2 rejected), accepted count and rejected
count. Guest code snapshots concurrency tokens before queuing a command. After
each step, core validation checks type, finite value, manifest bounds, generation
and parameter revision before committing. Explicit tokens in a command envelope
allow clients to reject stale updates. Ordinary HA number commands use a numeric
payload and the guest's latest snapshot.

One connection is allowed. All guest buffers, including zero-byte ranges, are
checked without integer wraparound. Handles are never reused within an engine
lifetime. Each call has a shared 2 KiB read/write allowance. The guest consumes
at most four protocol packets and one incoming publication per step. Its 2 KiB
RX, TX and retry buffers handle fragmented/coalesced packets and partial writes.
QoS 1 output allows one outstanding publication with DUP retransmission. Incoming
QoS 1 IDs are tracked in a bounded 8 KiB bitset, with duplicate acknowledgement
and no repeated parameter mutation. New non-DUP publications may reuse IDs.
Clean sessions, two subscriptions, retained discovery/availability/speed state,
retained QoS 1 Last Will, ten-second keepalive, one-second reconnect delay and
five-second connect/handshake timeouts are implemented in the guest.

The shared budget charges the resident animation source bytes, interpreter,
linear memory, animation buffers and the service's source/interpreter/memory.
Existing upload staging stays a separately reserved native buffer. Firmware never
allocates a second service before unloading its predecessor. Native socket/lwIP
allocations are outside the VM budget. C3 reports SDK free heap, lifetime minimum
free heap and current largest free block; those include all native services, not
an invented per-socket attribution. Pico's raw TCP adapter allocates a 2 KiB RX
buffer plus its control struct, with lwIP pools already reserved statically.
The experiment also reserves 9,216 static bytes for buffered timings.

## Management and HA example

Use the existing framed USB/HTTP protocol with `Client` from `tools/client.py`:

```python
client.upload_object("service", open("build/mqtt/service.wasm", "rb").read())
client.manage("service_load")
client.manage("service_start", ipv4=0x7f000001, port=18883)
client.manage("service_status")
print(client.data_reply.decode())
client.manage("service_stop")    # Close connections and retain the loaded guest.
client.manage("service_unload")  # Release code, interpreter and guest memory.
```

Staging retains existing ordered chunks, CRC checks, session ownership, retries,
timeouts and cancellation. `service_load` consumes a staged `kind=service` object;
it replaces only the service, even if admission fails. Services are volatile and
do not enter the normal persistent procedure catalog. Uploading/stopping/resetting
an animation does not implicitly stop its independent service.

HA discovery creates a speed number (-10..10, step .01) and VM heap/render-time/
command-count diagnostic sensors using the same native device identity. For ID
`esp32-...`, the command topic is `lightvm/esp32-.../speed/set`, accepting `0.3` or
`{"speed":0.3,"generation":7,"revision":0}`. The guest subscribes to
`homeassistant/status` and republishes discovery after HA's `online` birth.

Run Mosquitto on a separate loopback port, without using the normal broker:

```sh
docker run --rm -d --name lightvm-mqtt-experiment -p 127.0.0.1:18883:1883 \
  eclipse-mosquitto:2.0.22 sh -c \
  'printf "listener 1883\nallow_anonymous true\npersistence false\n" >/tmp/mosquitto.conf; exec mosquitto -c /tmp/mosquitto.conf'
PYTHONPATH=/tmp/lightvm-mqtt-tools python tools/mqtt_broker_check.py \
  --restart-command 'docker restart lightvm-mqtt-experiment'
docker stop lightvm-mqtt-experiment
```

The live check requires four valid retained discovery configs, availability, ten
commands per second plus 100-command burst, stale rejection, Last Will,
interrupted connection, broker restart, rediscovery and service reinitialization.
It runs the identical artifact in a standard one-page Wasmtime harness. This
checks HA-compatible exchange; testing Kaj's actual HA installation is deferred.

## C3 backup, measurement and restoration

Before deployment, preserve current active configuration/status and a **complete
4 MiB flash image**, including NVS, firmware, stored objects and boot journal.
Keep backups in ignored `build/` because NVS may contain Wi-Fi credentials.
Close all serial clients before using esptool. With the ESP-IDF Python active:

```sh
mkdir -p build/mqtt-c3-backup
python -m esptool --chip esp32c3 --port PORT --after no_reset \
  read_flash 0 0x400000 build/mqtt-c3-backup/flash.bin
shasum -a 256 build/mqtt-c3-backup/flash.bin
# Flash build/mqtt-c3/flash_args using esptool or idf.py.
python tools/benchmark_mqtt.py --port PORT --seconds 30 --repetitions 3 \
  --output build/mqtt/c3-measurements.json
# Always restore after measuring, including a failed run:
python -m esptool --chip esp32c3 --port PORT --after no_reset \
  write_flash 0 build/mqtt-c3-backup/flash.bin
python -m esptool --chip esp32c3 --port PORT --before no_reset --after hard_reset \
  verify_flash 0 build/mqtt-c3-backup/flash.bin
```

Recheck identity, original active/saved settings and stored procedures afterward.
The full-flash write restores the saved settings byte for byte; the boot journal
and runtime boot nonce naturally advance after boot. The runner also restores
the active output config and any named active procedure in a `finally` block.
It replaces a transient unnamed animation; use the full backup/restore workflow
on the dedicated test board. Never overwrite an existing backup with a new test.

Default windows are three repetitions of 30 seconds after five-second warmup.
The runner captures gaps in firmware, stops sampling, then reads buffered raw
samples so reporting does not stall the measured playback. It records FPS,
median/p95/p99/maximum gaps, missed intervals, allocation peaks and native heap.
The 2,048-gap buffer supports up to 33 seconds at the default 60 Hz; overflow is
explicit and prevents a performance pass. Transaction latency is **parse-to-core-
commit**, not broker-to-visible-light latency. MQTT timestamps in broker-check
output are host observation times; neither is an end-to-end hardware claim.

If admission fails, three real failures and the animation baselines are saved;
the runner also records three full windows containing the admission attempts.
Connected/traffic/reconnect windows are explicitly **unavailable**, not passes.
With sufficient memory and a reachable isolated broker, pass `--broker-ip`,
`--broker-port`, `--http-replacement-url` and `--restart-command` to exercise the
remaining scenarios. The HTTP URL must describe the same device identity.
Each completed window is checkpointed. After a transport failure, reconnect and
use `--resume` with the same output path; module, board and window settings must
match, and completed windows are retained.
Idle targets at least 95% baseline FPS and traffic at least 90%, with maximum
gap no greater than baseline maximum plus one configured interval. Physical
strip observation, Pico W execution and live HA installation remain separate
validation steps.
