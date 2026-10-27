# LightVM controllers and development workflow

LightVM runs animations on a controller, such as a Pico. The browser provides the
editor and sends commands to the controller. Animation versions and presets let
you save an effect and use it again.

## Controller concepts

A **controller** is the physical board, such as a Pico. Its firmware runs the
animations and sends data to the LEDs.

The browser compiles animation source and sends commands through USB, HTTP or WebSocket.
The controller continues playback after the browser disconnects.

These terms have specific meanings:

| Term | Meaning |
| --- | --- |
| **Output** | A named LED connection on a controller. Its configuration specifies the pin, LED count, colour order and brightness limit. |
| **Animation** | Editable Python source that defines an effect, such as Sweep. |
| **Program** | The compiled animation or transition code that the controller executes. |
| **Instance** | One execution of a program, with its own settings, time and state. |
| **Layer** | An animation instance with a position in the composition, an opacity and a blend mode. |
| **Composition** | The ordered layers that produce the image for one output. |
| **Preset** | A named, saved composition definition, including animation versions and layer settings. |
| **Transition** | A method of combining compositions A and B at a given progress, such as crossfade or wipe. |
| **Settings** | Initialization values for an animation or transition instance. |
| **Inputs** | Declared live values on a program instance, driven by constants, timers or named sources. |
| **Controls** | User interface controls for settings, input values and wiring. |

For example, one composition can contain two Sweep layers. One layer produces a red
eye. The other layer produces a blue eye. Both layers can use the same stored
program.

## Development workflow

Before you start, connect the controller. Configure at least one output.

The Editor workflow is **Edit → Preview → Save locally → Deploy to device**.
Edit an animation or transition and preview it on a configured output. Save its
editable source and preview settings to an external `.lightvm.json` file. Deploy
the compiled program to the device under a chosen name. Deployment alone does not
change playback.

Use **Configuration → Presets** to combine deployed animations, layer settings,
opacity and blend modes, and a preset transition. Save the recipe to the device.
Use **Mixer** to cue presets on A/B buses, choose a transition, mix manually or run
a timed fade. Saving a preset does not start playback.

## Storage and memory

The system uses three storage locations:

| Location | Contents |
| --- | --- |
| **External files** | Editable Python source and preview settings, downloaded/imported in Editor. Browser drafts remain session-only. |
| **Controller flash memory** | Compiled programs, controller presets and saved controller configuration. |
| **Controller RAM** | Active animation state and image data. |

Several presets can refer to one stored program. Each active animation instance
needs separate state and image memory. The controller checks available memory
before it replaces a composition.

Stored program and preset counts are limited by available storage, with separate
name namespaces for animations, transitions and presets. Backends and capacity are
selected by the board/build; see [internal storage](storage.md).
A composition supports up to eight layers, subject to available memory.

## Controls and sources

The output owns power, master brightness and selected RGB colour. Source adapters
publish music or sensor features. Preset bindings decide which input consumes a
control or source; the animation defines its visual meaning. Author-declared
control defaults make ordinary colour selection work without custom wiring.

The renderer produces unscaled layer contributions and A/B frames. A physical
output owns one admitted RGB dimming buffer (three bytes per pixel plus allocator
metadata), charged to the aggregate heap. It applies master brightness after
composition and pins that buffer for native asynchronous output. The hardware
brightness limit applies in the platform driver. Retained snapshots remain unscaled.

See [inputs](inputs.md) for binding precedence, snapshot boundaries, palette/range
mapping, revisions and persistence. Music publication does not replace bindings;
HA edits of the master dimmer do not take over musical intensity inputs.

## Startup

Startup is configured per output: off, restore the last preset, power and light controls,
or select a saved preset. The preset supplies the startup transition.

## Reference guides

- [Browser workspace](web-editor.md): editor files, device presets, mixing and connections.
- [Controller configuration](configuration.md): output configuration, startup and flash storage.
- [Animation profile](animation-profile.md): Python source, initialization and rendering.
- [Transition profile](transition-profile.md): named Python programs combining A/B compositions by progress.
- [Composition composition](composition.md): layer settings, blending and preset operations.
- [Control API](control-api.md): program execution, validation and management commands.
- [Native port interface](../src/platform.h): the services that controller firmware supplies to the engine.

## Names at integration boundaries

LightVM uses controller for the board and output for one LED connection. In HA,
the controller becomes a device grouping, each output becomes a light, and
all installed presets become its standard light effects. HA scenes can recall
these entities alongside other household equipment. They are separate from a
LightVM composition. Arbitrary layer settings are not exposed automatically.

The UI, Python/JavaScript clients and wire API share this vocabulary: configuration
and description use `outputs`, routing uses `output`, controller identity is
`controller_id`, composition operations use `composition_*`, stored code uses
`program_*`, and layer/startup references use `program` with initialization `settings`. Compiled packages keep
`lightvm.program`; saved compositions keep `lightvm.preset`.

## Clean installation after experimental changes

Earlier experimental configuration/preset documents and saved output state formats
are not accepted. LightVM 1.1 adds colour and brightness to the saved output state. Export animation source before reprovisioning. There are no API
aliases, fallback readers or automatic data migrations.

1. Host: start with a fresh storage path, for example
   `lightvm --storage build/current-store.bin` using a path that does not exist.
2. Embedded: erase experimental data before installing the current firmware.
   ESP32: use `idf.py -C platforms/esp32 -B BUILD_DIR erase-flash`, then flash the
   matching build. Pico: run Raspberry Pi's `flash_nuke.uf2` from BOOTSEL, then
   copy the current LightVM UF2. Both erase saved configuration and programs.
3. Reconnect with the current editor/client. Configure `outputs`, network access,
   and startup `program` references. Recompile and save animations, recreate
   presets, then configure each output's `home_assistant` binding.
4. Remove stale HA/MQTT discovery entries if output IDs or controller identities
   changed, or if the old binding was removed while offline. Current bindings
   publish one JSON light automatically. Clear the old retained
   `homeassistant/select/<controller_id>/<hex_output_id>_preset/config` with an empty
   retained payload, and clear the old output `preset/state` topic. Remove the old
   selector entity in HA. No legacy topic reader or automatic migration is retained.

A firmware-only update does not rewrite old saved configuration. Source files
using older experimental APIs must be updated and recompiled;
there is no import conversion.
