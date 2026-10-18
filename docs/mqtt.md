# MQTT control and Home Assistant lights

LightVM connects to an existing MQTT broker shared with Home Assistant. MQTT is
an optional control path alongside HTTP, WebSocket and USB/serial. The first binding exposes
**one on/off light per selected physical output**, with a saved preset assigned to
ON. It does not expose every preset as a separate light.

## Build

Add `-DLIGHTVM_MQTT=ON` to a host, Pico W or ESP32 build. It defaults OFF.
Embedded MQTT requires `LIGHTVM_WIFI=ON`; HTTP is independently optional.

```sh
cmake -S . -B build/mqtt-host -DLIGHTVM_PLATFORM=host -DLIGHTVM_MQTT=ON
cmake --build build/mqtt-host --parallel
build/mqtt-host/lightvm --storage build/mqtt-store.bin

cmake -S . -B build/mqtt-pico-w -DLIGHTVM_PLATFORM=pico -DPICO_BOARD=pico_w \
  -DPICO_SDK_PATH=/path/to/pico-sdk -DLIGHTVM_WIFI=ON -DLIGHTVM_MQTT=ON
cmake --build build/mqtt-pico-w --parallel

idf.py -C platforms/esp32 -B "$PWD/build/mqtt-c6" \
  -D LIGHTVM_BOARD=xiao_esp32c6 -D LIGHTVM_WIFI=ON -D LIGHTVM_MQTT=ON build
```

A host with saved MQTT enabled continues serving after stdin closes. Use its
storage file to keep a stable host device identity between launches. Embedded
identity comes from the board/MAC. USB provisioning remains available when MQTT
is disabled, disconnected or retrying. Power-recovery AP mode suspends MQTT for
that boot and retains the saved broker settings.

## Configure

1. Save the desired animation programs and a preset to the device through the
   existing editor or CLI. A preset can contain one animation or a layered scene.
2. Under **Configure → Output & startup**, choose **Home Assistant ON preset**
   for each output to expose. Optionally set its Home Assistant display name.
   Apply in RAM to try it, or apply and save for reboot.
3. Under the network settings, enable MQTT and enter the broker IPv4 address,
   port and credentials. Save and reconnect the network.
4. Connect Home Assistant's MQTT integration to that broker with discovery
   enabled and its default `homeassistant` discovery prefix and status topic.

There can be up to eight exposed outputs. Outputs without `home_assistant` stay
out of discovery and ignore MQTT commands. Names default to the output ID.
Changing the display name or assigned preset preserves the entity's unique ID;
changing the output ID creates a new entity.

The same settings are available in JSON. Add this optional object to each
physical device in `lightvm.config` (see `examples/config/mqtt-lights.json`):

```json
"home_assistant": {"on_preset": "evening", "name": "Desk lamp"}
```

`on_preset` accepts a preset name of 1–30 letters, digits, dots, underscores or
hyphens. `name` accepts 1–64 UTF-8 bytes. The preset is resolved when ON is
received; a missing or incompatible recipe leaves playback unchanged and puts
an error in `network_status.mqtt_error`. This permits assigning a recipe before
uploading it. Preset deletion is allowed; update the binding before removing a
recipe that should still be usable from HA.

Network settings extend `lightvm.network` with a separate `mqtt` object:

```json
{
  "schema": "lightvm.network",
  "wifi": {
    "enabled": true,
    "networks": [{"ssid": "studio", "password": "replace-wifi-password"}]
  },
  "http": {"enabled": true, "port": 80},
  "mqtt": {
    "enabled": true,
    "host": "192.168.1.10",
    "port": 1883,
    "username": "lightvm",
    "password": "replace-broker-password"
  }
}
```

For the host, leave Wi-Fi disabled with `networks:[]`; it uses the host network.
On an MQTT-only build, set `http.enabled:false` in the network document.
The original Pico remains USB-only. MQTT-only embedded builds can be provisioned
through USB, or enable HTTP as well to use the existing setup AP/editor flow.

```sh
python tools/client.py --port PORT --network examples/config/network-mqtt.json
python tools/client.py --port PORT --config examples/config/mqtt-lights.json --save
python tools/client.py --port PORT --network-status
```

Edit the example files for the actual broker, wiring and recipe before use. On
ESP32 add the usual `--no-reset`. Broker port defaults to 1883. Username and
password accept at most 64 UTF-8 bytes; a password requires a username. Empty
credentials select anonymous access. Omitting `mqtt` retains its settings;
omitting an MQTT field retains its current value, including the password. An
explicit empty password clears it. Readbacks omit the password, and the editor
keeps attempted passwords out of Redux/browser persistence and clears them after
saving. Credentials remain in device storage/RAM, as with Wi-Fi credentials.

This iteration uses **plain TCP MQTT 3.1.1 with numeric IPv4 broker addresses**.
It is intended for the trusted LAN. DNS names, TLS, MQTT 5 properties, QoS2,
custom discovery prefixes, brightness, colour and additional channels are outside
this iteration. Home Assistant and LightVM may use different supported protocol
versions on the same broker.

## Topics and power behavior

`device_id` comes from `describe`. `output_id` is the hardware device's `id`.

| Topic | Payload / behavior |
| --- | --- |
| `lightvm/<device_id>/<output_id>/set` | Exact `ON` or `OFF`; publish without retain. |
| `lightvm/<device_id>/<output_id>/state` | Retained actual playback state, `ON` or `OFF`. |
| `lightvm/<device_id>/availability` | Retained `online`; retained Last Will `offline`. |
| `homeassistant/light/<device_id>/<hex_output_id>/config` | Retained MQTT light discovery document. |
| `homeassistant/status` | HA's `online` birth message triggers discovery/state/availability refresh. |

Discovery encodes the output ID as lowercase UTF-8 hex to satisfy HA's topic
identifier restrictions without collisions (for example `desk` becomes
`6465736b`). State and command topics use the original ID. Every light has a
stable unique ID and all outputs are grouped under one controller device in HA.
Discovery uses HA's default light schema, advertises only power control, supplies
a state topic and explicitly disables retained commands. See the official
[MQTT discovery](https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery)
and [MQTT light](https://www.home-assistant.io/integrations/light.mqtt/) contracts.

ON while stopped prepares the assigned preset and starts it on the entire output,
replacing an existing A/B presenter as well as its scenes. Failed preparation
preserves the old runtime. Repeated ON while already playing leaves that runtime
running, including playback started by HTTP or USB; it does not restart effects.
OFF stops the output and presents black pixels. State tracks the output's playing
flag, rather than estimating brightness from pixel values, and reports changes
made through MQTT, HTTP, WebSocket or serial. Other physical outputs continue independently.

Commands with a retained flag, unknown devices and unsupported payloads are
ignored. Retained commands must not be used for boot recovery. Each reconnect
uses a clean broker session and resubscribes; queued offline commands are not
replayed. Subscriptions request QoS0, outgoing publications use QoS0, and the
receiver also acknowledges incoming QoS1. Power commands are idempotent.

The client uses a 30-second keepalive, sends idle PINGs after 15 seconds, and
retries failures after 1, 2, 4, 8, 16 and then 30 seconds. Connection/subscription
replies time out after 10 seconds and stalled writes after 5 seconds. Discovery,
state and availability are republished after reconnect and HA startup. Losing
the network or disabling/reconfiguring MQTT closes the session so the broker's
Last Will marks it offline; silent radio/power loss can take the broker's
keepalive timeout to be detected. Playback continues during MQTT outages.

Removing an exposed output or its binding clears its retained discovery and state
on the connected broker. Removal is retried across reconnects during the same
firmware run. If you change brokers, wipe storage, or reboot after removing a
binding while offline, remove the old discovery entries on the old broker/HA
explicitly; no persistent discovery tombstone journal is maintained. Disabling
MQTT retains discovery but makes the controller unavailable.

## Separation and verification

`src/mqtt.[ch]` owns bounded MQTT framing, sessions, buffering, keepalive,
subscriptions, publications and reconnect policy. It knows nothing about App,
outputs, presets or HA. Its nonblocking stream contract has a shared host/ESP32
socket adapter and a Pico W lwIP raw adapter. No network callbacks run engine
commands, and no worker thread owns the VM.

`src/home_assistant.[ch]` owns topic/discovery conventions, output bindings and
state observation. It dispatches power commands through the ordinary application
command boundary. The runtime, recipe validation, peak-memory admission and
failure behavior remain shared with HTTP/WebSocket/serial. Calling `preset_activate`
without a slot replaces the complete output; explicitly selecting `A` or `B`
updates that slot under the existing transition owner.

The generic client reserves 2 KiB for receive and 2 KiB for transmit buffers. Work
per tick and published messages per binding tick are bounded; buffer pressure
retries without blocking the animation loop. `describe` reports `mqtt_supported`;
`network_status` adds `mqtt_connected` and `mqtt_error`. Connected means the broker
accepted CONNECT; discovery follows successful subscription acknowledgments.

```sh
ctest --test-dir build/mqtt-host --output-on-failure
LIGHTVM_RECEIVER="$PWD/build/mqtt-host/lightvm" python -m unittest discover -s tests -p test_mqtt.py -v
```

C transport tests use fragmented and stalled streams, malformed/oversized packets,
rejected handshakes/subscriptions and timeouts. Python tests use a local observable
MQTT peer with the real runtime, flash storage and compiled animation/preset. They
cover independent lights, black output on OFF, A/B takeover, concurrent HTTP, WebSocket and
serial control, retained/invalid commands, missing presets, HA restart, reconnect,
reboot, credential redaction and discovery removal. Editor tests cover preset
assignment and broker-password handling. CI builds both MQTT-only and combined
HTTP/MQTT profiles. Physical radios and a real Home Assistant installation still
need an end-to-end acceptance check.
