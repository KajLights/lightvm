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
| **Controls** | User interface inputs; future live bindings will use this term rather than calling them settings. |

For example, one composition can contain two Sweep layers. One layer produces a red
eye. The other layer produces a blue eye. Both layers can use the same stored
program.

## Development workflow

Before you start, connect the controller. Configure at least one output.

The development workflow has five main steps:

1. **Edit.**
   Write or change the animation source. Set the required animation settings.
   The browser checks and compiles the source.

2. **Preview.**
   Select a configured output, such as the output connected to GP16.
   Click **Preview animation**.

   The controller replaces playback on that output. Other outputs continue
   playback. Change the source or settings as necessary. Repeat the preview.

3. **Save animation.**
   Enter an animation name. Click **Save animation**.

   The browser saves the source, compiled program and selected default settings.
   Each save creates an animation version. Existing presets keep their saved
   versions.

4. **Compose, if necessary.**
   Select saved animation versions. Add the required layers. Set the layer order,
   settings, opacity and blend modes. Click **Preview preset**.

   A preset can contain one or more layers.

5. **Save preset.**
   Enter a preset name. Click **Save preset** to save the definition in the browser.
   Click **Save preset to controller** to save the compiled programs and preset
   definition on the controller.

Select a saved controller preset. Click **Run on output**.
Saving a preset does not start playback.

## Storage and memory

The system uses three storage locations:

| Location | Contents |
| --- | --- |
| **Browser storage** | Editable animation versions and presets. **Export library** creates a backup file. |
| **Controller flash memory** | Compiled programs, controller presets and saved controller configuration. |
| **Controller RAM** | Active animation state and image data. |

Several presets can refer to one stored program. Each active animation instance
needs separate state and image memory. The controller checks available memory
before it replaces a composition.

Stored program and preset counts are limited by available storage, with separate
name namespaces for animations, transitions and presets. Backends and capacity are
selected by the board/build; see [internal storage](storage.md).
A composition supports up to eight layers, subject to available memory.

## Startup

Startup is a separate setting. Each output can start one stored program when the
controller starts. Saved presets currently require an explicit run command.

## Reference guides

- [Browser workspace](web-editor.md): editor controls, library storage and connections.
- [Controller configuration](configuration.md): output configuration, startup and flash storage.
- [Animation profile](animation-profile.md): Python source, initialization and rendering.
- [Transition profile](transition-profile.md): named Python programs combining A/B compositions by progress.
- [Composition composition](composition.md): layer settings, blending and preset operations.
- [Control API](control-api.md): program execution, validation and management commands.
- [Native port interface](../src/platform.h): the services that controller firmware supplies to the engine.

## Names at integration boundaries

LightVM uses controller for the board and output for one LED connection. In HA,
the controller becomes a device grouping, each output becomes a light, and its
exposed presets become options in a Preset select entity. HA scenes can recall
these entities alongside other household equipment. They are separate from a
LightVM composition. Arbitrary layer settings are not exposed automatically.

The UI, Python/JavaScript clients and wire API share this vocabulary: configuration
and description use `outputs`, routing uses `output`, controller identity is
`controller_id`, composition operations use `composition_*`, stored code uses
`program_*`, and layer/startup references use `program` with initialization `settings`. Compiled packages keep
`lightvm.program`; saved compositions keep `lightvm.preset`.

## Clean installation after the naming change

Earlier experimental configuration/preset documents use different field names and
are not accepted. Export animation source before reprovisioning. There are no API
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
   publish both the light and Preset selector automatically.

A firmware-only update does not rewrite old saved configuration. Browser library
exports containing older compiled source APIs must be recreated from source;
there is no import conversion.
