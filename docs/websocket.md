# Optional plain WebSocket management

`LIGHTVM_WEBSOCKET` adds an RFC 6455 server at
`ws://<device>:<http-port>/api/v1/ws`. It defaults **OFF**, requires
`LIGHTVM_HTTP=ON`, and uses the existing HTTP listener, enable setting, and port.
Embedded builds also require `LIGHTVM_WIFI=ON`. USB, HTTP commands, discovery,
and WebSocket clients share the serialized engine and may run concurrently.
The discovery-only port 80 listener does not accept upgrades when the command
listener uses a custom port.

This module provides plain `ws://` only, with the same trusted-LAN access policy
as HTTP: no authentication or encryption. It accepts browser origins; clients
still need any local-network permission their browser requires. An HTTPS-hosted
editor must satisfy that browser's mixed-content/local-network rules. The web
editor's existing transport selection is unchanged; enabling this firmware
module does not automatically move its fader onto WebSockets.

## Build

Add `-DLIGHTVM_WEBSOCKET=ON` to an HTTP-enabled build. For the host simulator:

```sh
cmake -S . -B build/ws-host -DLIGHTVM_HTTP=ON -DLIGHTVM_WEBSOCKET=ON
cmake --build build/ws-host --parallel
build/ws-host/lightvm --http 8080
```

For Pico W, using the pinned SDK/toolchain from the main build instructions:

```sh
cmake -S . -B build/ws-pico-w -DLIGHTVM_PLATFORM=pico -DPICO_BOARD=pico_w \
  -DLIGHTVM_WIFI=ON -DLIGHTVM_HTTP=ON -DLIGHTVM_WEBSOCKET=ON \
  -DPICO_SDK_PATH=/path/to/pico-sdk
cmake --build build/ws-pico-w --parallel
```

For an activated ESP-IDF 5.5.3 environment:

```sh
idf.py -C platforms/esp32 -B "$PWD/build/ws-c3" \
  -D LIGHTVM_BOARD=xiao_esp32c3 -D LIGHTVM_WIFI=ON \
  -D LIGHTVM_HTTP=ON -D LIGHTVM_WEBSOCKET=ON build
```

The same option supports `xiao_esp32s3` and `xiao_esp32c6`. Optional
`LIGHTVM_BOOT_SEQUENCE` behavior is unchanged. With WebSockets disabled, its
source, parser state, and native integration are excluded; `/api/v1/ws` returns
404. `describe.websocket_supported` reports whether it was compiled in. There
is no new persisted configuration or migration.

## Wire contract

The client sends a normal HTTP/1.1 WebSocket upgrade to `/api/v1/ws`, with a
version of 13 and a valid 16-byte `Sec-WebSocket-Key`. No extensions, compression,
or subprotocol are negotiated. An unsupported version returns HTTP 426 with
`Sec-WebSocket-Version: 13`; malformed upgrades return 400.

Each **binary WebSocket message contains exactly one complete LightVM command
frame**: the existing 16-byte `LVM1` header and at most 512 payload bytes. CRC,
protocol-version, request-ID, and command validation remain the same as HTTP.
The message may span continuation frames or arbitrary TCP reads. Masking is
required on every client frame. Text messages close with 1003; malformed framing
or LightVM frames close with 1002; oversized messages close with 1009.

One binary server message contains the command's complete response: one or more
ordinary LightVM response frames, ending in `RESP_STATUS`. It can exceed 528
bytes for downloads and snapshots. Feed its bytes to the existing LightVM
response decoder. Server frames are unmasked. Ping/pong and the closing handshake
are supported, including control frames interleaved with fragmented messages.

The socket owns a unique engine source identity. An HTTP session token is not
needed or accepted in place of that identity. Closing the socket releases its
in-progress upload or staged candidate. Reconnection creates a new identity;
clients must inspect current state rather than replay an uncertain mutation.

## Bounds and scheduling

The portable HTTP queue admits four pending requests and uses one bounded response
buffer across both transports. ESP32 also retains its existing four-entry native
HTTP ingress queue; it transfers a request when the portable engine is free. Each WebSocket has a fixed input buffer plus
framing/control state; it does not allocate a second large response buffer or
create a server task. ESP32 retains its existing asynchronous sender task;
socket reads are nonblocking, and an output barrier confirms sender progress
before completing an upgrade or reply. Small WebSocket traffic disables Nagle.

Idle upgraded sockets remain connected. Incomplete frames/fragmented messages
and stalled response writes expire after two seconds without progress. Queue
exhaustion closes the affected WebSocket with 1013 after previously accepted
work; it does not accumulate an unbounded backlog. Native connection limits are
shared with HTTP: four on Pico W, six on ESP32, eight on the host. Clients should
bound outstanding work and coalesce fader positions to the latest value.

## Measured footprint

Matching Wi-Fi/HTTP/power-sequence builds with only `LIGHTVM_WEBSOCKET` toggled
produced the following deltas with the pinned toolchains (Pico Release and
ESP-IDF 5.5.3 performance defaults):

| Profile | Additional firmware bytes | Additional static RAM |
| --- | ---: | ---: |
| Pico W | 4,904 | 3,024 |
| ESP32-C3 | 6,784 | 4,512 |

These totals cover the port's entire fixed connection capacity, not a per-client
allocation. ESP32 also adds 48 bytes to its existing dynamically allocated ingress
and sender queue storage. TCP/Wi-Fi runtime allocations depend on live connections
and traffic and are not included in the static-RAM figures. No new task stack or
large response buffer is reserved. Pico W retains 28,192 bytes after the project's
RAM reservations. All ESP32 profiles fit the current app partition; C6 has roughly
44 KiB remaining in the measured WebSocket build.

This is a build-size comparison, not a hardware latency or peak-heap benchmark.
Firmware was cross-compiled without flashing a device.

## Verification

```sh
ctest --test-dir build/ws-host --output-on-failure
LIGHTVM_RECEIVER="$PWD/build/ws-host/lightvm" \
  python -B -m unittest discover -s tests -p 'test_websocket.py' -v
```

The C contract tests exercise the RFC handshake vector, partial writes,
asynchronous output completion, serialized dispatch, deferred disconnect
cleanup, and timeout handling. Socket integration tests cover persistent
commands, fragmented/partial/coalesced frames, ping/close, malformed frames,
upload/download, ownership release, and concurrent USB-pipe/HTTP/WebSocket
clients. The firmware CI matrix includes enabled and disabled builds for host,
Pico W, and all three ESP32 profiles.
