# MQTT control and Home Assistant presets

LightVM connects to an existing MQTT broker shared with Home Assistant. MQTT is
an optional control path alongside HTTP, WebSocket and USB/serial. Each exposed
output has **one on/off light and one Preset selector**, grouped under its
controller's HA device. A preset recalls a complete composition, including its
animation versions, layer settings, opacity and blending. A one-layer composition
works exactly the same way. Composition editing stays in LightVM.

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
storage file to keep a stable host controller identity between launches. Embedded
identity comes from the board/MAC. USB provisioning remains available when MQTT
is disabled, disconnected or retrying. Power-recovery AP mode suspends MQTT for
that boot and retains the saved broker settings.

## Configure

1. Save the desired animation programs and the presets to the controller through the
   existing editor or CLI. A preset can contain one animation or a layered composition.
2. Under **Configure → Output & startup**, choose **Home Assistant default preset**
   for each output to expose, then check the presets to include in its selector.
   Optionally set its Home Assistant display name.
   Apply in RAM to try it, or apply and save for reboot.
3. Under the network settings, enable MQTT and enter the broker hostname or IPv4 address,
   port and credentials. Save and reconnect the network.
4. Connect Home Assistant's MQTT integration to that broker with discovery
   enabled and its default `homeassistant` discovery prefix and status topic.

There can be up to eight exposed outputs. Outputs without `home_assistant` stay
out of discovery and ignore MQTT commands. Names default to the output ID.
Changing the display name or assigned preset preserves both entities' unique IDs;
changing the output ID creates new entities.

The same settings are available in JSON. Add this optional object to each
physical output in `lightvm.config` (see `examples/config/mqtt-lights.json`):

```json
"home_assistant": {
  "default_preset": "evening",
  "presets": ["evening", "rainbow", "stars"],
  "name": "Desk lamp"
}
```

`presets` is a required list of 1–16 unique preset names for that output.
`default_preset` must appear in it. Names contain 1–32 letters, digits, dots,
underscores or hyphens. The name `None` is reserved by HA for unknown select state
and cannot be exposed. `name` accepts 1–64 UTF-8 bytes. These limits also remain
subject to the controller's configuration size and JSON token budget.

Only presets currently stored on the controller appear in HA's options. Saving,
replacing or deleting a preset refreshes discovery; changing the output's list
also refreshes it. Bindings may refer to presets before upload. A missing or
incompatible preset leaves playback unchanged and reports an error in
`network_status.mqtt_error`. Configure each output's list for its pixel count and
available memory; activation still validates the full composition on that output.

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
    "host": "mqtt.example.com",
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
saving. Credentials remain in controller storage/RAM, as with Wi-Fi credentials.

This iteration uses **plain TCP MQTT 3.1.1 over IPv4**. Broker addresses may be
numeric IPv4 addresses or DNS hostnames up to 127 ASCII bytes, with labels up to
63 bytes. Do not include a scheme or port in `host`; set the separate `port`
field. Pico W uses DHCP-provided DNS servers and asynchronous lwIP resolution.
Host and ESP32 use their system resolver on a bounded background worker, so DNS
does not block playback. Cancelled or timed-out lookups cannot connect to an old
broker after settings change. A failed lookup follows the MQTT reconnect policy.
It is intended for the trusted LAN. TLS, MQTT 5 properties, QoS2,
custom discovery prefixes, brightness, colour and additional channels are outside
this iteration. Home Assistant and LightVM may use different supported protocol
versions on the same broker.

## Topics and power behavior

`controller_id` comes from `describe`. `output_id` is the output’s `id`.

| Topic | Payload / behavior |
| --- | --- |
| `homeassistant/lightvm/<controller_id>/<output_id>/set` | Exact `ON` or `OFF`; publish without retain. |
| `homeassistant/lightvm/<controller_id>/<output_id>/preset/set` | Preset name from this output’s list; starts it, without retain. |
| `homeassistant/lightvm/<controller_id>/<output_id>/preset/state` | Retained preset name, or `None` for unknown. |
| `homeassistant/lightvm/<controller_id>/<output_id>/state` | Retained accepted power target, `ON` or `OFF`; rendering may continue during a fade. |
| `homeassistant/lightvm/<controller_id>/availability` | Retained `online`; retained Last Will `offline`. |
| `homeassistant/light/<controller_id>/<hex_output_id>/config` | Retained MQTT light discovery document. |
| `homeassistant/select/<controller_id>/<hex_output_id>_preset/config` | Retained MQTT Select discovery document. |
| `homeassistant/status` | HA's `online` birth message triggers discovery/state/availability refresh. |

All binding topics use the default `homeassistant/` namespace. Allow the controller
account to read and write `homeassistant/#` on brokers with topic ACLs. Home
Assistant learns command, state and availability topics from discovery; no manual
entity YAML or topic configuration is needed.

Discovery encodes the output ID as lowercase UTF-8 hex to satisfy HA's topic
identifier restrictions without collisions (for example `desk` becomes
`6465736b`). State and command topics use the original ID. Every light has a
stable unique ID and all outputs are grouped under one device representing the controller in HA.
Discovery uses HA's default light schema, advertises only power control, supplies
a state topic and explicitly disables retained commands. See the official
[MQTT discovery](https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery)
and [MQTT light](https://www.home-assistant.io/integrations/light.mqtt/) contracts.
The accompanying [MQTT Select](https://www.home-assistant.io/integrations/select.mqtt/)
entity is named `<output name> Preset` and reports confirmed state.

Selecting a preset invokes the application's recall operation: it completes the
previous destination, moves it into A if necessary, prepares the new preset in B,
and runs its entry transition forward. ON recalls the last identifiable, exposed
selection or `default_preset`, using the output's on transition when configured.
OFF transitions to black using the off transition and retains the selection.
Preset entry and output power transition configuration are described in
[recall transitions](transitions.md#preset-recalls-and-power).

Repeated ON/OFF or selection of the current target does not restart it. Failed
preparation preserves the old runtime, driver and selection. A reboot starts with
no selection until a preset is activated; startup remains separately configured.

Power and preset state reflect changes through MQTT, HTTP, WebSocket and serial.
Recall publishes the accepted preset and logical power target even while the
physical transition is running. OFF does not mean the rendering engine has stopped.
Manual A/B controls keep fixed slots and release recall ownership. An intermediate
manual mix, edited layers, unsaved compositions or changed/deleted recipe versions
report unknown preset state; an endpoint can identify its unmodified preset.
`describe.outputs[].preset` remains the actual visible endpoint identity, while
`selected_preset` and `transition_get.recall` expose the recall selection separately.

For example, turn on an output and recall a preset in HA:

```yaml
- action: select.select_option
  target:
    entity_id: select.desk_lamp_preset
  data:
    option: stars
```

Commands with a retained flag, unknown outputs and unsupported payloads are
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

Removing an exposed output or its binding clears both retained discovery documents and both state topics
on the connected broker. Removal is retried across reconnects during the same
firmware run. If you change brokers, wipe storage, or reboot after removing a
binding while offline, remove the old discovery entries on the old broker/HA
explicitly; no persistent discovery tombstone journal is maintained. Disabling
MQTT retains discovery but makes the controller unavailable.

## Keypad and RGB indicators

Pico W can use the Pimoroni Pico RGB Keypad Base as an HA automation controller.
Under **Configure → Output & startup**, choose **Add Pico RGB Keypad**, then
**Apply & save for reboot**. This adds generic TCA9555 inputs and one APA102
indicator output. The Advanced JSON editor supports custom labels and bindings.
The same configuration is in
[`pico-rgb-keypad.json`](../examples/config/pico-rgb-keypad.json):

```sh
python tools/client.py --port PORT --config examples/config/pico-rgb-keypad.json --save
python tools/client.py --port PORT --network examples/config/network-mqtt.json
```

Set the actual broker credentials in the network example first. Applying the
keypad example replaces the active output configuration; combine its `outputs`
and `inputs` with existing outputs when keeping other lights. An original Pico
supports the peripherals over USB, but standalone MQTT needs Pico W networking.

HA discovers **one controller device**, containing sixteen RGB light entities and
32 device triggers (press and release for each key). Existing HA output lights
and preset selectors also belong to that same controller device. The RGB lights
support on/off, brightness and RGB colour through the MQTT JSON light schema;
transition and flash capabilities are explicitly disabled in discovery.
Pressing a key does not locally toggle its indicator: HA automations decide both
what the press does and what the indicator should display. For example, choose
the `keypad_key_1` short-press trigger to toggle a kitchen lamp, then use another
automation on that lamp's reported state to set Key 1 indicator green or dim.
Use explicit light entities when targeting an automation action; targeting the
whole keypad device would affect all its lights. No custom HA integration or
manual discovery YAML is needed. These follow HA's official
[device trigger](https://www.home-assistant.io/integrations/device_trigger.mqtt/)
and [JSON RGB light](https://www.home-assistant.io/integrations/light.mqtt/#json-schema)
contracts.

The default wiring is SDA GP4, SCL GP5, expander address `0x20`, APA102 data GP19,
clock GP18 and active-low bus enable GP17. Brightness limit defaults to 64/255
in the example/editor preset. Drivers contain no keypad-specific pin mapping;
see [Pimoroni's reference](https://github.com/pimoroni/pimoroni-pico/blob/main/libraries/pico_rgb_keypad/pico_rgb_keypad.cpp).
Physical key-to-LED ordering and timing still need acceptance on the actual board.

For input ID `keypad` and one-based key number `n` (1..16):

| Topic | Payload / behavior |
| --- | --- |
| `homeassistant/lightvm/<controller_id>/input/keypad/<n>/event` | Non-retained `press` or `release`. |
| `homeassistant/lightvm/<controller_id>/input/keypad/<n>/set` | JSON light command, e.g. `{"state":"ON","brightness":128,"color":{"r":0,"g":255,"b":0}}`. Publish without retain. |
| `homeassistant/lightvm/<controller_id>/input/keypad/<n>/state` | Retained accepted light target, including state, brightness, RGB and `color_mode:"rgb"`. |
| `homeassistant/lightvm/<controller_id>/input/keypad/<n>/availability` | Retained indicator output health, combined with controller availability. |
| `homeassistant/light/<controller_id>/i_<hex_input_id>_<n>/config` | Retained RGB-light discovery. |
| `homeassistant/device_automation/<controller_id>/i_<hex_input_id>_<n>_press/config` | Retained press-trigger discovery; release uses `_release/config`. |

The input's `home_assistant` may omit `indicators` for press/release triggers
without lights. Each trigger's subtype is `<input_id>_key_<n>` and its type is
`button_short_press` or `button_short_release`; long/double press recognition is
not implemented. Physical held state and expander availability are reported by
`describe.inputs`; loss/recovery does not manufacture presses. Triggers have no
availability field in HA's MQTT device-trigger contract.

There are 64 ordered events across all inputs, with a 250 ms publication lifetime
and a cumulative `describe.input_event_overflow` counter. Offline, reconnected and
HA-restarted sessions discard pending events. Publications use QoS0; this is not
guaranteed or exactly-once automation execution. Native queue expiry precedes
MQTT queueing; an already queued network packet cannot be recalled. Retained
commands and malformed/unsupported light fields are rejected without partial
state changes. Rendering continues while MQTT is unavailable.

RGB state survives MQTT reconnects and HA restarts in controller RAM. Reboot
starts indicators off; transient light values are not written to flash. HA
automations can restore desired indications when the controller becomes available.
Changing unrelated configuration preserves unchanged indicator hardware and state.
Removing an HA input binding clears its light and trigger discovery and retained
light state/availability, including retries across reconnects during the same
firmware run. The existing limitation on offline removal followed by reboot
applies here too.

Host replay uses `LIGHTVM_I2C_INPUTS=/absolute/path/inputs.bin`: eight little-endian
16-bit raw input masks, indexed by addresses `0x20`..`0x27`. Both simulated buses
read that same address bank. Missing/truncated files represent disconnected
hardware; write via atomic file replacement to avoid accidental simulated loss.
This is a host test adapter; Pico uses the physical I2C bus.

```sh
LIGHTVM_RECEIVER="$PWD/build/mqtt-host/lightvm" python -m unittest discover -s tests -p test_keypad.py -v
```

## Separation and verification

`src/mqtt.[ch]` owns bounded MQTT framing, sessions, buffering, keepalive,
subscriptions, publications and reconnect policy. It knows nothing about App,
outputs, presets or HA. Its nonblocking stream contract has a shared host/ESP32
socket adapter and a Pico W lwIP raw adapter with asynchronous DNS. No network
callbacks run engine commands, and no worker thread owns the VM.

`src/home_assistant.[ch]` owns topic/discovery conventions, output bindings and
state observation. It dispatches power and preset commands through the ordinary application
command boundary. The runtime, recipe validation, peak-memory admission and
failure behavior remain shared with HTTP/WebSocket/serial. HA uses `preset_recall`
and `output_power`. `preset_activate` prepares a fixed slot (A by default),
retaining its transition owner and progress.

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
rejected handshakes/subscriptions and timeouts. Pico DNS tests cover cached and
asynchronous results, failures, cancellation, late replies after reconfiguration,
and bounded outstanding lookups. Python tests use a local observable
MQTT peer with the real runtime, flash storage and compiled animation/preset. They
cover independent lights, black output on OFF, A/B takeover, concurrent HTTP, WebSocket and
serial control, retained/invalid commands, missing presets, HA restart, reconnect,
reboot, credential redaction and discovery removal. Editor tests cover preset
assignment and broker-password handling. CI builds both MQTT-only and combined
HTTP/MQTT profiles.

Pico W hardware acceptance verified hostname resolution through DHCP-provided
DNS, authenticated broker connection, retained HA discovery and availability,
MQTT OFF with all 200 pixels black, ON activating the selected preset, serial
changes reflected in MQTT state, and concurrent HTTP/WebSocket access. Saved
programs, network credentials and output bindings survived firmware deployment.
ESP32 C6 and S3 profiles cross-build successfully; their physical radios and the
Home Assistant frontend still need an end-to-end acceptance check.
