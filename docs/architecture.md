# LightVM controllers and development workflow

LightVM runs animations on a controller, such as a Pico. The browser provides the
editor and sends commands to the controller. Animation versions and presets let
you save an effect and use it again.

## Controller concepts

A **controller** is the physical board, such as a Pico. Its firmware runs the
animations and sends data to the LEDs.

The browser compiles animation source and sends commands through USB or HTTP.
The controller continues playback after the browser disconnects.

These terms have specific meanings:

| Term | Meaning |
| --- | --- |
| **Output** | A named LED connection on a controller. Its configuration specifies the pin, LED count, colour order and brightness limit. |
| **Animation** | Editable Python source that defines an effect, such as Sweep. |
| **Program** | The compiled animation code that the controller executes. |
| **Instance** | One execution of a program, with its own settings, time and state. |
| **Layer** | An animation instance with a position in the composition, an opacity and a blend mode. |
| **Scene** | The ordered layers that produce the image for one output. |
| **Preset** | A saved definition of the layers, animation versions and settings needed to create a scene. |

For example, one scene can contain two Sweep layers. One layer produces a red
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
   Click **Save preset to device** to save the compiled programs and preset
   definition on the controller.

Select a saved device preset. Click **Run on output**.
Saving a preset does not start playback.

## Storage and memory

The system uses three storage locations:

| Location | Contents |
| --- | --- |
| **Browser storage** | Editable animation versions and presets. **Export library** creates a backup file. |
| **Controller flash memory** | Compiled programs, device presets and saved controller configuration. |
| **Controller RAM** | Active animation state and image data. |

Several presets can refer to one stored program. Each active animation instance
needs separate state and image memory. The controller checks available memory
before it replaces a scene.

Stored program and preset counts are limited by available storage, with separate
name namespaces for animations, transitions and presets. Backends and capacity are
selected by the board/build; see [internal storage](storage.md).
A scene supports up to eight layers, subject to available memory.

## Startup

Startup is a separate setting. Each output can start one stored program when the
controller starts. Saved presets currently require an explicit run command.

## Reference guides

- [Browser workspace](web-editor.md): editor controls, library storage and connections.
- [Device configuration](configuration.md): output configuration, startup and flash storage.
- [Animation profile](animation-profile.md): Python source, initialization and rendering.
- [Transition profile](transition-profile.md): named Python programs combining A/B scenes by progress.
- [Scene composition](composition.md): layer settings, blending and preset operations.
- [Control API](control-api.md): program execution, validation and management commands.
- [Native port interface](../src/platform.h): the services that controller firmware supplies to the engine.
