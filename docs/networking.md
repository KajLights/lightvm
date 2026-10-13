# Concurrent USB and HTTP management

Networking is optional firmware functionality. The original Pico remains USB-only.
Pico W, XIAO ESP32-S3 and XIAO ESP32-C6 can include Wi-Fi and HTTP. USB continues
working when networking is disabled, unavailable or reconnecting. All paths share
one runtime, library and configuration; commands execute between engine steps.

Wi-Fi devices advertise a DHCP hostname of `lightvm-xxxxxx`, using the last three
bytes of the station MAC address as six lowercase hexadecimal digits, as in
picolights (for example, `lightvm-0421e0`). The name is stable across reboots and
network changes. Routers that register DHCP names in local DNS can resolve it;
the DNS suffix, if any, depends on the router.

HTTP has **no authentication or encryption**. It is intended for your trusted LAN.
The server exposes commands and discovery metadata, not the editor's static files.
Firmware OTA, MQTT, HTTPS and source storage are not included. AP mode serves
the same full API; the editor remains hosted separately.

## Build

Existing builds default to `LIGHTVM_WIFI=OFF` and `LIGHTVM_HTTP=OFF`. Embedded
HTTP requires Wi-Fi; Wi-Fi without HTTP is supported. Use separate build directories.

```sh
idf.py -C platforms/esp32 -B "$PWD/build/network-c6" \
  -D LIGHTVM_BOARD=xiao_esp32c6 -D LIGHTVM_WIFI=ON -D LIGHTVM_HTTP=ON build
# Substitute xiao_esp32s3 and a separate directory for S3.

# Pico SDK 1.5.1, with its pinned radio and network submodules:
git -C PATH_TO_PICO_SDK submodule update --init lib/tinyusb lib/cyw43-driver lib/lwip
cmake -S . -B build/network-pico-w -DLIGHTVM_PLATFORM=pico -DPICO_BOARD=pico_w \
  -DPICO_SDK_PATH=PATH_TO_PICO_SDK -DLIGHTVM_WIFI=ON -DLIGHTVM_HTTP=ON
cmake --build build/network-pico-w

cmake -S . -B build/network-host -DLIGHTVM_PLATFORM=host -DLIGHTVM_HTTP=ON
cmake --build build/network-host
build/network-host/lightvm --storage build/http-store.bin --http 8001
```

Host HTTP binds to `127.0.0.1`; `--http-bind IPV4` changes this explicitly. Port
zero chooses an available test port, printed to stderr. Pipes remain available;
closing stdin leaves an explicitly HTTP-enabled host running. The `--http` option
is a development listener override, independent of saved radio settings. Without
that option, an HTTP-enabled saved network configuration starts a loopback
listener on its saved port at boot; the host has no physical radio.

Pico W networking profiles retain the 32 KiB module limit and use a 64 KiB VM heap
ceiling. The radio uses PIO1; WS2812 remains on PIO0. Bounded lwIP pools and the
reply buffer are included in the linker footprint. The build emits
`ram-report.json` and rejects reservations that cannot retain 16 KiB operating
headroom after VM, stack and pixel-driver ceilings. This is a reservation check;
radio timing, actual native allocations and stack peaks still require hardware
measurement. USB-only profiles retain their original ceilings.

## Initial setup and standalone AP mode

On Pico W, XIAO ESP32-S3 and XIAO ESP32-C6 builds with both `LIGHTVM_WIFI=ON`
and `LIGHTVM_HTTP=ON`, a device without a valid saved network configuration
starts an **open** access point named `lightvm-xxxxxx`, using the same stable
station-MAC suffix as its hostname. Join it and connect the editor or CLI to
`http://192.168.4.1`. The radio uses channel 1 and DHCP assigns up to four addresses
from `192.168.4.2` through `192.168.4.5` on a `/24` subnet.

This setup configuration exists only in RAM. `network_get` returns it and
`network_status` reports `initial_setup:true`. Save network settings to choose
station mode, permanent AP mode or disabled networking. A failed save keeps the
setup AP available; a successful save applies after the reply grace period.
A valid saved configuration always takes precedence, including disabled Wi-Fi.
Station connection failures keep retrying the saved list and never open an AP.
Wi-Fi-only builds can use saved AP mode through USB but do not start a setup AP.
USB-only builds and the host simulator do not start an AP.

The editor's **Wi-Fi & HTTP** panel offers **Join Wi-Fi networks** and **Host an
access point** on firmware advertising `wifi_ap_supported:true`. Only one mode
runs at a time; switching retains both station and AP credentials. An omitted
mode selects `station` for compatibility with existing configurations.

For standalone operation, save a configuration such as
[`examples/network-ap.json`](../examples/network-ap.json):

```json
{
  "schema": "lightvm.network/1",
  "wifi": {
    "enabled": true,
    "mode": "ap",
    "country": "XX",
    "networks": [],
    "ap": {"ssid": "", "password": "replace-this-password"}
  },
  "http": {"enabled": true, "port": 80}
}
```

Within `wifi.ap`, an empty or omitted SSID selects `lightvm-xxxxxx`; custom SSIDs contain 1–32
UTF-8 bytes. New AP settings default to open. AP passwords are empty for open or
8–63 printable ASCII characters for WPA2-Personal. Omit `ap.password` to retain
it, including when changing SSID; set it to `""` to explicitly open the AP.
Omitting the whole AP object retains its settings. When updating existing
settings, an omitted network list retains the station networks; an explicit
empty list removes them. Passwords are omitted from all network readbacks.
Saved AP mode respects the HTTP enable switch and port. Its status uses
`ap_starting` / `ap_active`, with `mode:"ap"` and the actual SSID, IP and port.

The API, USB management and animation engine remain available together.
Discovery advertises on the AP interface, including `lightvm.local`; use the
fixed IP when `.local` resolution is unavailable. A custom HTTP port retains
the discovery-only listener on port 80, as in station mode.

AP mode provides local device access without internet routing or a captive
portal. Open the hosted editor before joining the AP, or build and serve it on
localhost following [the editor instructions](web-editor.md#run-locally).
Visit Develop while online if source compilation will be needed: its external
compiler/editor assets must already be loaded, and offline caching is not
guaranteed. Stored programs and precompiled WASM can be managed through the API
without those compiler assets. Recovery buttons and automatic AP fallback after
station failures are deferred.

## Configure station Wi-Fi through USB

The editor's **Wi-Fi & HTTP** panel can add, reorder and remove networks. Enable
Wi-Fi and HTTP, then **Save & reconnect network**. The existing serial CLI also
accepts a JSON file:

```json
{
  "schema": "lightvm.network/1",
  "wifi": {
    "enabled": true,
    "country": "XX",
    "networks": [
      {"ssid": "preferred", "password": "replace-this-password"},
      {"ssid": "backup", "password": "another-password", "hidden": true}
    ]
  },
  "http": {"enabled": true, "port": 80}
}
```

```sh
python tools/client.py --port PORT --no-reset --network network.json
python tools/client.py --port PORT --no-reset --network-status
python tools/client.py --url http://DEVICE_IP --describe
python tools/client.py --url http://DEVICE_IP --get-config
python tools/client.py --url http://DEVICE_IP --store-procedure rainbow rainbow.wasm
python tools/client.py --url http://DEVICE_IP --activate rainbow
python tools/client.py --url http://DEVICE_IP --set-parameters '{"speed":0.3}'
```

Omit `--no-reset` for Pico/Pico W. `--get-network` omits passwords;
`--validate-network FILE` checks without saving. After initial USB provisioning,
network settings may be changed through either path. Network changes preserve the
program and output configuration, but can terminate HTTP access. Read the new IP
through USB or your router, then reconnect; the editor can fill the address from
USB status. The editor also finds devices through the discovery endpoint below;
direct addresses remain available when multicast is blocked.

## Local device discovery

Wi-Fi/HTTP firmware on Pico W and both ESP32 profiles advertises
`_lightvm._tcp.local` with its command port, stable device ID, board and API version.
Each board also answers the shared `lightvm.local` IPv4 name. This bootstrap name
may reach any LightVM board; each board retains its unique `lightvm-xxxxxx.local`
name, probes for collisions and chooses another suffix if necessary.

The editor fetches `http://lightvm.local/api/v1/devices` automatically and displays
a **Nearby devices** picker. The board performs DNS-SD queries; the browser only
uses HTTP. Select a device and click **Connect HTTP**. Reads and commands use the
selected IP and port, so the shared name cannot switch boards during a session.
USB and known HTTP addresses can seed discovery when `.local` resolution is
unavailable. **Refresh** retries discovery, and background refresh runs every ten
seconds while the editor is visible. Discovery never creates command sessions,
changes the selected command path, or replays a command.

The unauthenticated, read-only endpoint is also available to other tools:

```sh
curl http://lightvm.local/api/v1/devices
```

It returns `schema:"lightvm.discovery/1"`, `available`, `capacity:8`, `truncated`
and `devices`. The first entry is the responding board; entries contain
`hostname`, `device_id`, `board`, `ip`, `port` and `url`. The cache holds this board
and up to seven peers. Incomplete records are hidden, graceful shutdown removes
peers immediately, and silent departures expire within 30 seconds. A full cache
sets `truncated:true` instead of allocating or evicting live devices.

Discovery starts when the station is connected or the AP is active and the
command listener is ready.
Radio loss, disablement and reconfiguration clear the cache; IP/port changes
restart announcements. If commands use another port, a discovery-only listener
remains on port 80. It exposes `/api/v1/devices`, not command sessions. Both
listeners share existing bounded ingress and response buffers. ESP32 HTTP builds
reserve sixteen lwIP sockets for the two servers, clients and multicast socket;
existing build-local SDK configurations must adopt `CONFIG_LWIP_MAX_SOCKETS=16`.

The shared implementation is IPv4-only and scoped to LightVM's DNS-SD records,
with 1024-byte packets, bounded DNS compression traversal, delayed multicast
answers, known-answer suppression and no heap-owned device list. Multicast is
local to the LAN; isolated guest networks and VLANs need an existing multicast
forwarder or a direct address. Browser local-network permission still applies.
The host simulator returns itself for HTTP/UI testing and does not advertise on
the LAN. USB-only and Wi-Fi-only builds omit discovery entirely.

Network settings have their own durable revision and do not form part of
`lightvm.config/1`. A commit validates a staged candidate, checks its expected
revision, atomically saves it, then schedules reconfiguration after its reply
(or after one second if the reply cannot be delivered). A short transmit grace
allows accepted response bytes to leave the native sender. A failed save retains
the previous settings. Connection failures retain the newly saved list for retries.

At most eight unique SSIDs are supported. SSIDs contain 1–32 UTF-8 bytes;
Unicode escapes and surrogate pairs are decoded without truncation. Passwords
are empty for open networks, 8–63 printable ASCII characters for WPA2-Personal,
or 64 hexadecimal digits. An omitted password retains the existing password for
that exact SSID; a new SSID needs an explicit password, including `""` for open.
`hidden` defaults false, country defaults `XX` (worldwide), and HTTP port defaults
80. Country values use two uppercase letters; native radio errors remain visible
in status. Enterprise authentication and WPA3-only networks are outside this
shared radio profile. Credentials remain in the device's snapshot storage and RAM;
network readbacks omit them and the editor never saves them in browser storage.

## Reconnection policy

The asynchronous controller scans and tries visible networks in configured order,
including direct attempts for hidden entries. Scans expire after ten seconds;
connection and DHCP attempts expire after fifteen. Definite failures advance
immediately. Scanning and connecting never run concurrently.

After exhausting the list, retries rescan after 5, 10, 20, 40 and then 60 seconds,
with ±20% jitter. Successful connection resets the delay. Losing a connection
retries that SSID once before returning to the ordered scan cycle. A healthy
connection is retained even if a preferred SSID reappears. Failed driver
initialization is retried with the same bounded backoff. No reboot or fallback access point
is used for recovery. USB and animation scheduling continue.

`network_status` reports `state`, `mode`, `initial_setup`, `ssid`, `ip`, `error`,
`retry_ms`, `revision` and `http_port`. Discovery distinguishes compiled
`wifi_supported`, `wifi_ap_supported` and `http_supported` capabilities from
configured service availability.

## HTTP wire contract

| Request | Reply |
|---|---|
| `GET /api/v1/devices` | 200 JSON: bounded DNS-SD device list; no session required |
| `POST /api/v1/sessions`, empty body | 201 JSON: `session_id`, `device_id`, `boot_id`, `expires_in_ms` |
| `POST /api/v1/command`, `X-LightVM-Session` header | 200 binary response frames ending in STATUS |
| `DELETE /api/v1/sessions/{session_id}` | 204; release that session's candidate |
| `OPTIONS` | CORS preflight for GET/POST/DELETE, Content-Type and X-LightVM-Session |

Command bodies use `application/octet-stream` and exactly one protocol-v1 frame,
including its existing 16-byte header and CRC. Responses concatenate OUTPUT,
DATA or PIXELS frames and terminal STATUS. Device command errors still return
200 with their existing nonzero STATUS. HTTP-level errors include 400 for an
invalid frame/request, 404 for an unknown/expired session or path, 405 for a wrong
method, 413 for an oversized body, 415 for a wrong command media type and 503 for
exhausted capacity. Cookies, redirects, chunked request bodies and command replay
are not used. CORS permits origins without credentials.

Four HTTP sessions are permitted, each expiring after five minutes without
commands. Session IDs identify reply/upload ownership and are not authentication
tokens. Four pending HTTP commands and one bounded response are supported. The
engine never waits for an HTTP sender; stalled sends expire after two seconds
without progress. Radio loss invalidates HTTP sessions. Reconnect creates a new
session and refreshes runtime generations/revisions.

Each connection owns its decoder and response route. Request IDs can overlap.
Uploads and finished candidates belong to one connection and expire after thirty
seconds of inactivity. Other connections cannot append, finish, consume or cancel
that candidate. Closing an HTTP session, a detected USB cable loss, or the
`session_close` management operation releases it. New clients send `session_close`
on orderly serial close; old clients and undetectable serial application closures
use the inactivity timeout. RESET remains an explicit device-wide runtime reset.

New management operations are `network_get`, `network_status`,
`network_validate`, `network_commit` and `session_close`. Network uploads use
OBJECT_BEGIN with `kind:"network"`. Commit uses `expected_revision` from discovery's
`network_revision`. Discovery adds `connection_api:1`, stable `device_id`,
per-boot `boot_id`, feature support, network revision and the effective VM heap limit.
No guest ABI or protocol-version change is required.

Saved snapshots now use directory version 2, preserving version-1 read support
and eight procedure slots plus reserved `$config`/`$network` objects. Upgrading
keeps existing stored programs and configuration without repartitioning. Older
firmware may recover an older bank or reject new snapshots; downgrading cannot be
relied upon to preserve recent data. Network settings are independent of startup
configuration, including failed hardware initialization.

After any lost mutation reply, reconnect and read state before deciding whether
to repeat it. A successful save may have occurred even when its reply was lost.

## Verification

Host C tests cover ownership, expiry, parser rejection, snapshot interruption and
radio policy using a fake asynchronous radio. Python tests exercise real wasm3
through concurrent pipes and HTTP, including large and stalled downloads,
credentials, reboot/migration and parameter conflicts. Browser tests verify path
identity/selection and forbid automatic replay after a lost HTTP response.

```sh
ctest --test-dir build/network-host --output-on-failure
LIGHTVM_RECEIVER=build/network-host/lightvm python -m unittest discover -s tests -v
node --test tests/test_editor.mjs
```

The optional `tools/check_editor_browser.cjs` acceptance check requires Playwright,
Chrome/Chromium with local-network permission support, and access to the pinned
compiler assets. It starts disposable host/editor servers, exercises the panels,
and writes desktop/mobile screenshots under `build/`. Set `PLAYWRIGHT_MODULE` to
an installed Playwright package if it is outside the usual Node module path,
`CHROME_EXECUTABLE` for a particular browser, and `LIGHTVM_PYTHON` if the Python
environment is outside `build/venv`. The serial API is simulated through a second
HTTP session; native pipe/HTTP concurrency is covered by the Python suite.

```sh
LIGHTVM_RECEIVER=build/network-host/lightvm node tools/check_editor_browser.cjs
```

### Local verification, 12 October 2026

- Host and 175-pixel profiles: C contracts pass with USB-only and HTTP builds;
  91 Python integration tests pass on each HTTP profile, and 80 pass on each
  USB-only profile (the HTTP class is skipped). Both host smoke checks pass.
- Nine Node protocol/editor tests pass. The Chrome acceptance check passes
  both paths, network settings, password clearing/browser-storage exclusion,
  conflict refresh, parameter updates, pause/resume, procedure save/download/
  activate/delete, startup selection, and mobile layout without horizontal overflow.
- All six Python effects upload and render at 1,024 pixels under the 64 KiB VM
  ceiling in a host build using the Pico W resource profile. Observed cumulative
  VM heap peak is 32,783 bytes; this does not measure the native radio heap.
- All ten embedded variants below build. Images fit the existing flash/storage
  layout; the C6 HTTP image leaves 74,816 bytes in its 1 MiB application partition.
  CI workflow validation and GCC warning checks pass.

| Board | Transport features | Application `.bin` bytes | Static RAM bytes |
|---|---|---:|---:|
| Pico | USB | 207,740 | 81,292 |
| Pico W | USB | 207,340 | 81,292 |
| Pico W | USB + Wi-Fi | 481,284 | 105,760 |
| Pico W | USB + Wi-Fi + HTTP | 548,080 | 159,208 |
| XIAO ESP32-C6 | USB | 266,416 | 137,788 |
| XIAO ESP32-C6 | USB + Wi-Fi | 922,448 | 225,332 |
| XIAO ESP32-C6 | USB + Wi-Fi + HTTP | 973,760 | 265,138 |
| XIAO ESP32-S3 | USB | 303,280 | 153,623 |
| XIAO ESP32-S3 | USB + Wi-Fi | 811,920 | 203,995 |
| XIAO ESP32-S3 | USB + Wi-Fi + HTTP | 854,896 | 243,715 |

Pico static RAM is GNU `size` data plus BSS. ESP32 figures sum SDK-reported RAM
regions, including resident code and RTC reservations. Neither includes peak
dynamic allocations. Pico W HTTP retains 9,752 bytes beyond its separate
64 KiB VM, 16 KiB stack, 3 KiB pixel-driver and 16 KiB operating-headroom
reservations; Wi-Fi without HTTP retains 63,200 bytes beyond those reservations.

Cross-builds establish compilation and declared capacity, not physical radio
correctness. Hardware acceptance still requires Wi-Fi-capable boards: provision,
power cycle, fail the first SSID, recover after AP loss, and use USB/HTTP concurrently
while measuring output, stack and native heap under load.

### Pico W deployment, 12 October 2026

The attached board was identified by the previous firmware as `pico`; that was
its USB-only build profile, rather than evidence of the physical board model.
After backing up its configuration and stored program, the Pico W Wi-Fi/HTTP UF2
was flashed over BOOTSEL. Its image ends below the reserved snapshot-storage area.
USB verification confirms `board:pico_w`, Wi-Fi and HTTP capabilities, the 64 KiB
VM ceiling, and the retained 200-pixel GP16/GRB configuration at brightness 32.
The saved `rainbow` program resumes with no boot or animation error and 11,870
bytes of VM heap in use. Network settings are initially disabled; Wi-Fi provisioning
and the wireless acceptance checks remain pending.

### Device discovery validation, 12 October 2026

The discovery builds pass all four host C contracts, all 92 Python integration
tests and all 12 editor Node tests. DNS tests exercise two-board exchange,
compressed names, record truncation, cache limits, TTL expiry, goodbye records,
shared aliases, legacy unicast replies, known-answer suppression and unique-name
conflicts. Packet mutations also pass AddressSanitizer and UndefinedBehaviorSanitizer.
The Chrome acceptance check verifies automatic bootstrap, IP selection, HTTP and
USB concurrency, compiler/upload/management flows, stable focused form controls
and mobile layout. Its simulator bootstrap DNS name is routed to the real host;
the live Pico W check below additionally verifies actual multicast and Wi-Fi.

| Board | Discovery application `.bin` bytes | Static RAM bytes |
|---|---:|---:|
| Pico W | 563,568 | 162,940 |
| XIAO ESP32-C6 | 985,312 | 269,002 |
| XIAO ESP32-S3 | 865,024 | 247,555 |

The Pico comparison against the same pre-discovery source/toolchain is +15,424
bytes of flash and +3,716 static bytes, leaving 6,020 bytes after the existing RAM
reservations. The C6 application partition retains 63,264 bytes. These figures
exclude peak dynamic transport/task allocations; a non-default command port
starts an additional ESP32 HTTP task for the discovery-only listener. Fresh ESP32
builds use the updated sixteen-socket SDK default. USB-only host and Wi-Fi-only
Pico builds also pass. ESP32 physical radio validation remains pending.

### Pico W discovery deployment, 12 October 2026

The connected Pico W's active/saved output configuration and stored `rainbow`
module were backed up and checked before flashing the discovery UF2. Every UF2
block targets flash below `0x10180000`, preserving snapshot storage. After boot,
the configurations and module bytes match the backup, Wi-Fi reconnects using the
saved credentials, and the 200-pixel GP16/GRB output retains brightness limit 100
and its `rainbow` startup program. USB and HTTP report no boot or animation error;
the active VM heap remains 11,870 bytes and playback continues during HTTP reads.

Actual `lightvm.local` resolution and `GET /api/v1/devices` both succeed. macOS
Bonjour browsing discovers `_lightvm._tcp.local` as `lightvm-0160a2`. A Chrome
check using actual DNS, with no bootstrap interception, automatically finds the
Pico W, selects its IP, connects over HTTP and displays the 200-pixel slot A
binding without page errors. The check closes its session without changing the
program or configuration. Multi-board physical discovery, AP-loss recovery and
sustained resource measurements remain separate acceptance work.

## AP validation on hardware

For each Wi-Fi board, check a fresh or invalid configuration boots to the open
setup AP, obtains DHCP leases, and answers discovery and all existing command
endpoints. Exercise configuration, library uploads/activation and parameters
while animation playback and USB management continue. Verify a failed save
leaves setup accessible, a successful save replies before switching interfaces,
and saved open/WPA2 AP settings survive reboot. Repeat AP/station transitions,
including custom HTTP ports and explicitly disabled networking. AP startup and
radio loss should retry without interrupting the VM; a missing station network
must not trigger an AP. These checks require physical boards and complement the
portable policy, DHCP packet, storage, HTTP and editor tests.
