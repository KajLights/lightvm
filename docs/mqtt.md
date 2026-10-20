# MQTT control and Home Assistant presets

LightVM connects to an existing MQTT broker shared with Home Assistant. MQTT is
an optional control path alongside HTTP, WebSocket and USB/serial. Each exposed
output has **one light with power, brightness, colour and preset effects**, grouped under its
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
   for each output to expose, then check the presets to include in its effect list.
   Optionally set its Home Assistant display name.
   Apply in RAM to try it, or apply and save for reboot.
3. Under the network settings, enable MQTT and enter the broker hostname or IPv4 address,
   port and credentials. Save and reconnect the network.
4. Connect Home Assistant's MQTT integration to that broker with discovery
   enabled and its default `homeassistant` discovery prefix and status topic.

There can be up to eight exposed outputs. Outputs without `home_assistant` stay
out of discovery and ignore MQTT commands. Names default to the output ID.
Changing the display name or assigned preset preserves the light's unique ID;
changing the output ID creates a new entity.

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
underscores or hyphens. The names `off` and `Custom` are reserved and cannot be exposed. `name` accepts 1–64 UTF-8 bytes. These limits also remain
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
custom discovery prefixes and additional channels are outside this iteration. Home Assistant and LightVM may use different supported protocol
versions on the same broker.

## Topics and light behavior

`controller_id` comes from `describe`. `output_id` is the output’s `id`.

| Topic | Payload / behavior |
| --- | --- |
| `homeassistant/lightvm/<controller_id>/<output_id>/set` | JSON light command; publish without retain. |
| `homeassistant/lightvm/<controller_id>/<output_id>/state` | Retained JSON accepted light state. |
| `homeassistant/lightvm/<controller_id>/availability` | Retained `online`; retained Last Will `offline`. |
| `homeassistant/light/<controller_id>/<hex_output_id>/config` | Retained MQTT JSON light discovery. |
| `homeassistant/status` | HA's `online` birth triggers discovery/state/availability refresh. |

All binding topics use the default `homeassistant/` namespace. Allow the controller
account to read and write `homeassistant/#` on brokers with topic ACLs. HA learns
all topics from discovery; no manual entity YAML is needed. Discovery encodes the
output ID as lowercase UTF-8 hex (for example `desk` becomes `6465736b`). The light
keeps its stable unique ID and belongs to its controller's HA device.

A command can contain any combination of `state` (`ON` or `OFF`), `brightness`
(0–255), `color` (complete integer `r`, `g`, `b` channels in 0–255) and `effect`
(an exposed preset name). Optional `color_mode` must be `rgb`. Unknown fields and
invalid values reject the entire command before changing controls or playback.
Retained commands are ignored. Discovery disables HA flash and transition commands;
preset entry transitions supply the transition behavior.

```json
{"state":"ON","effect":"eye","brightness":128,"color":{"r":255,"g":128,"b":192}}
```

Brightness is the output master dimmer, applied once after composition and A/B
mixing, before the configured hardware brightness limit. Music can independently
drive an animation's local intensity. Colour is a latched full RGB selection,
consumed only by inputs bound to `{"control":"color"}`. Commands without state
or effect update controls without turning an off light on. Brightness zero means
OFF in the HA adapter, preserving the previous dimmer level for the next ON.

State reports the accepted power, master brightness, selected RGB and current
`effect`. `Custom` identifies an edited, private or unidentified composition.
`color_mode` is `rgb` when a visible animation follows light colour; otherwise it
is `brightness`, as permitted for active effects. Colour remains stored for a
later colour-following preset. Audio fluctuations do not become HA light state;
a black musical frame remains logically on. Pausing retains the light and allows
master dimming without advancing animation time.

See HA's [MQTT JSON light contract](https://www.home-assistant.io/integrations/light.mqtt/#json-schema)
and [effect colour modes](https://developers.home-assistant.io/docs/core/entity/light/#color-mode-while-an-effect-runs).

Selecting a preset invokes the application's recall operation: it completes the
previous destination, moves it into A if necessary, prepares the new preset in B,
and runs its entry transition forward. ON recalls the last identifiable, exposed
selection or `default_preset`, using that preset's saved transition.
OFF uses the selected preset's same transition to black and retains the selection.
Startup uses the same preset-owned transition. A preset with no transition cuts
immediately; there are no output-level transition overrides.
Preset transition configuration and power behavior are described in
[recall transitions](transitions.md#preset-recalls-and-power).

Repeated ON/OFF or selection of the current target does not restart it. Failed
preparation preserves the old runtime, driver and selection. A reboot starts with
no selection until a preset is activated or restored. Each output separately
configures its power-on behavior as off, restore last state, or a selected preset;
see [startup configuration](configuration.md#json-configuration-v1). MQTT
reconnect and HA restart do not reapply the controller’s startup policy.

Power and preset state reflect changes through MQTT, HTTP, WebSocket and serial.
Recall publishes the accepted preset and logical power target even while the
physical transition is running. OFF does not mean the rendering engine has stopped.
Manual A/B controls keep fixed slots and release recall ownership. An intermediate
manual mix, edited layers, unsaved compositions or changed/deleted recipe versions
report the `Custom` effect; an endpoint can identify its unmodified preset.
`describe.outputs[].preset` remains the actual visible endpoint identity, while
`selected_preset` and `transition_get.recall` expose the recall selection separately.

For example, turn on an output and recall a preset in HA:

```yaml
- action: light.turn_on
  target:
    entity_id: light.desk_lamp
  data:
    effect: stars
    brightness_pct: 30
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

HA discovers **one controller device**, containing sixteen button event entities,
sixteen RGB light entities and sixteen **Press feedback** configuration switches.
Existing HA output lights also belong to that same controller device. The RGB lights support on/off,
brightness, RGB colour and effects through the MQTT JSON light schema; transition
and flash are disabled in discovery. HA automations decide what the gesture does
and what the indicator should display. Optional local press feedback temporarily
brightens the indicator while held. For example, use Key 1's `press_start` event to toggle a kitchen
lamp immediately, then use the lamp's reported state to set the Key 1 indicator.
Use explicit light entities when targeting an automation action; targeting the
whole keypad device would affect all its lights. No custom HA integration or
manual discovery YAML is needed. These follow HA's official
[MQTT event](https://www.home-assistant.io/integrations/event.mqtt/),
[standard button event](https://contributors.home-assistant.io/core/entities/event/#standard-event-types)
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
| `homeassistant/lightvm/<controller_id>/input/keypad/<n>/event` | Non-retained JSON, e.g. `{"event_type":"press_start"}` or `{"event_type":"multi_press_end","multi_press_count":2}`. |
| `homeassistant/lightvm/<controller_id>/input/keypad/<n>/event/availability` | Retained input acquisition health, combined with controller availability by the event entity. |
| `homeassistant/lightvm/<controller_id>/input/keypad/<n>/set` | JSON light command, e.g. `{"state":"ON","brightness":128,"color":{"r":0,"g":255,"b":0},"effect":"slow_blink"}`. Publish without retain. |
| `homeassistant/lightvm/<controller_id>/input/keypad/<n>/state` | Retained accepted light target, including state, brightness, RGB, effect and `color_mode:"rgb"`. Blink phases and pressed brightness do not change this target. |
| `homeassistant/lightvm/<controller_id>/input/keypad/<n>/press_feedback/set` | Exact `ON` or `OFF`; automatically persists this key's feedback setting. Publish without retain. |
| `homeassistant/lightvm/<controller_id>/input/keypad/<n>/press_feedback/state` | Retained accepted feedback setting, `ON` or `OFF`. |
| `homeassistant/lightvm/<controller_id>/input/keypad/<n>/availability` | Retained indicator output health, combined with controller availability. |
| `homeassistant/light/<controller_id>/i_<hex_input_id>_<n>/config` | Retained RGB-light discovery. |
| `homeassistant/switch/<controller_id>/i_<hex_input_id>_<n>_press_feedback/config` | Retained feedback-switch discovery with `entity_category:"config"`; requires controller, input and indicator availability. |
| `homeassistant/event/<controller_id>/i_<hex_input_id>_<n>/config` | Retained button event discovery, with `device_class:"button"`. |

The input's `home_assistant` may omit `indicators` for events without lights or feedback switches.
Each channel has its own stable event identity and independent gesture state.
Labels can change without changing identity. Physical held state and expander
availability are also reported by `describe.inputs`.

### Indicator effects and press feedback

Each indicator advertises these effect names in HA:

| Effect | Local pattern |
| --- | --- |
| `steady` | Constant configured colour and brightness; the default. |
| `slow_blink` | 500 ms on, 500 ms off. |
| `rapid_blink` | 125 ms on, 125 ms off. |
| `010100` | Repeating off/on/off/on/off/off, with 150 ms per step (900 ms per cycle). |

Select an effect together with the usual light fields. Quote `"010100"` in YAML
so it remains a string. For example, replacing the entity ID with your indicator:

```yaml
action: light.turn_on
target:
  entity_id: light.key_1_indicator
data:
  rgb_color: [0, 255, 0]
  brightness_pct: 20
  effect: "010100"
```

An omitted effect keeps the previous selection. Changing effects or turning an
off indicator on starts the pattern at its first step. Other updates preserve
the phase, including selecting the same effect again. Timing uses the local
monotonic clock; missed frames skip ahead rather than slowing down the pattern.

Each **Key N press feedback** switch defaults off. Enabling it saves the setting
on the controller immediately; no separate onboard save or HA restore automation
is needed. Changes require writable storage and are rejected if saving fails.
Settings follow the input ID and key number across renames, output rebinding,
configuration changes and reboot. Turning a switch off persists that change too.

While a feedback-enabled key is down, its indicator displays the configured RGB
colour continuously at brightness 255, still subject to the physical output's
`brightness_limit`. This overrides blinking and the light's off state; RGB black
still renders black. It uses the debounced physical level (including a key held
at boot) and works without MQTT or HA. Indicator rendering also runs at cooperative
runtime checkpoints so other animation work can yield to feedback. HA light state remains the requested target.
Updates while held take effect immediately, including colour changes. Release
resumes the latest target at its current blink phase. Disabling feedback or losing
input acquisition removes the override. Presses never write to flash.

### Gesture semantics and timing

Recognition runs in firmware on debounced edges and a monotonic clock, following
the Matter momentary switch model with release, long-press and multi-press
support (MS/MSR/MSL/MSM, without ActionSwitch), as described in the
[Matter Switch cluster specification, sections 1.13.7–1.13.9](https://csa-iot.org/wp-content/uploads/2025/08/3-27350_matter-1-4-2-adopted-application-cluster-specification.pdf).
This is an MQTT interface using
HA's standard vocabulary; it does not add a Matter transport or certification.

| Event type | Emitted when |
| --- | --- |
| `press_start` | Each accepted down edge, immediately after debounce. |
| `press_end` | Each up edge in a short/multi-press cycle. |
| `long_press_start` | The first press reaches `long_press_ms`, once while held. |
| `long_press_end` | That standalone long hold is released. |
| `multi_press_ongoing` | The second or subsequent down edge, after `press_start`, with `multi_press_count`. |
| `multi_press_end` | The input stays released for `multi_press_ms`, with the completed `multi_press_count` (including 1). |

Defaults are `long_press_ms:500` and `multi_press_ms:300`, configurable per input
adapter in `config`. Timing starts at accepted debounced edges; the multi-press
window runs from the last release to the next accepted down edge. At the exact
idle deadline the old sequence completes before a new press starts. Timing
recognition has the 5 ms sampling resolution and is subject to service stalls.
Timer events retain their actual deadline for the publication expiry check.

A single tap emits `press_start`, `press_end`, then `multi_press_end` with count 1
after the idle window. Two taps emit `press_start`, `press_end`, `press_start`,
`multi_press_ongoing` with count 2, `press_end`, then `multi_press_end` with count 2.
A standalone hold emits `press_start`, `long_press_start`, `long_press_end`, with
no short release or completed single-tap event. As required by the Matter model,
a hold on a later press in an existing multi-press cycle remains part of that
cycle: it ends with `press_end`, without long-press events. Up to 255 presses are
counted. Longer sequences suppress further start/ongoing events and end with
count 0 (aborted), rather than wrapping to a misleading single or double press.

Choose `press_start` for an immediate action; it also occurs on holds and on each
tap of a double press. Choose `press_end` for an action on each release without
the multi-press delay. For mutually exclusive single/double/triple actions, use
`multi_press_end` and filter `multi_press_count` to 1/2/3. Use `long_press_start`
and `long_press_end` to start and stop a hold action such as dimming. Short-release
and ongoing events describe progress and can coexist with a later completed
gesture; they are not exclusive single-click results.

There are 64 ordered events across all inputs, with a 250 ms publication lifetime
and a cumulative `describe.input_event_overflow` counter. Offline, reconnected,
HA-restarted and reconfigured sessions discard queued events and cancel unfinished
gestures. Hardware loss does the same for the affected adapter. Boot/recovery
establishes the current level silently; a key already held must be released and
pressed again before it can generate a gesture. This also avoids orphaned release
or hold events after a session reset.

Publications use QoS0; this is not guaranteed or exactly-once automation execution.
Native queue expiry precedes MQTT queueing; an already queued network packet
cannot be recalled. Retained commands and malformed/unsupported light fields are
rejected without partial state changes. Rendering continues while MQTT is unavailable.

RGB state and effect selection survive MQTT reconnects and HA restarts in controller RAM. Reboot
starts targets off, white, brightness 255 and `steady`; transient light values are not written to flash.
Saved press-feedback switches remain enabled, so a held key can still illuminate. HA
automations can restore desired indications when the controller becomes available.
Changing unrelated configuration preserves unchanged indicator hardware and state.
Removing an HA input binding clears its light, feedback-switch and event discovery and retained
light/feedback state and input/output availability, including retries across reconnects
during the same firmware run. The existing limitation on offline removal followed by reboot
applies here too.
Stored feedback settings remain associated with their input ID if a binding is
removed and later re-added. Disable feedback before removing it to reset the setting.

Host replay uses `LIGHTVM_I2C_INPUTS=/absolute/path/inputs.bin`: eight little-endian
16-bit raw input masks, indexed by addresses `0x20`..`0x27`. Both simulated buses
read that same address bank. Missing/truncated files represent disconnected
hardware; write via atomic file replacement to avoid accidental simulated loss.
This is a host test adapter; Pico uses the physical I2C bus.

```sh
LIGHTVM_RECEIVER="$PWD/build/mqtt-host/lightvm" python -m unittest discover -s tests -p test_keypad.py -v
```

### Replacing the earlier experimental keypad interface

The former plain `press`/`release` payloads and MQTT device triggers are removed.
Before provisioning the new firmware, stop the old publisher and clear its
retained `homeassistant/device_automation/<controller_id>/i_<hex_input_id>_<n>_press/config`
and corresponding `_release/config` topics for each key (publish an empty retained
payload). Remove or recreate automations that reference those old triggers, using
the new event entities. The example input ID `keypad` is hex `6b6579706164` in these
discovery paths. No storage migration is provided or needed; reapply/save the
current configuration for a clean reprovision.

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

Keypad tests also cover effect discovery and atomic commands, local feedback
without MQTT, per-key persistence across process restarts, retained-command rejection,
input loss, and switch cleanup. Clock-controlled C tests verify the blink steps,
feedback precedence, asynchronous frame ownership and storage failure handling.

Pico W hardware acceptance verified hostname resolution through DHCP-provided
DNS, authenticated broker connection, retained HA discovery and availability,
MQTT OFF with all 200 pixels black, ON activating the selected preset, serial
changes reflected in MQTT state, and concurrent HTTP/WebSocket access. Saved
programs, network credentials and output bindings survived firmware deployment.
ESP32 C6 and S3 profiles cross-build successfully; their physical radios and the
Home Assistant frontend still need an end-to-end acceptance check.
