# Browser controller workspace

The static React/TypeScript app has four primary tabs, in order: **Editor**,
**Configuration**, **Mixer**, and **Audio Playground**. It opens in Editor.
Connections and firmware installation are utilities in the header. Editor and
local audio tuning work without a device; Configuration and Mixer need a connected
controller. Drafts survive tab changes within the session.

## Editor: edit → preview → save locally → deploy

Editor handles animations and transitions in Python, JavaScript and Blocks. Choose a canonical example
or **Import file** (`.py`, `.js`, `.blocks.json` or `.lightvm.json`), edit the source, then:

For a device-free start, choose a lesson and use **Try it here → Start preview**.
The [teaching guide](teaching.md) describes the local strip/ring preview, stepping,
saved settings and input values, and the small beginner Blocks toolbox.

1. **Preview on device** runs the current compilation on the selected physical
   output. Animation preview replaces playback there. For transitions, first cue
   A and B in Mixer; preview stages `_preview_transition` on the device and runs a
   two-second fade towards the opposite endpoint. Firmware requires a stored name
   for transitions, so this single scratch entry remains in flash until replaced
   or deleted. It does not overwrite the named deployment.
2. **Save locally** downloads a `.lightvm.json` file containing the original editable source,
   filename, program name, preview settings and input bindings. It works offline
   from a controller and even while code has errors. Import that file to resume.
   **Code & exports** also offers raw `.py`, `.js` or `.blocks.json` and compiled `.lbc` downloads.
3. **Deploy to device** stores the current compiled animation or transition under
   its program name, without selecting it or changing playback. Configure preset
   layer settings and input bindings under Configuration. Source remains in the
   external file; compiled device programs cannot restore editable source.

Preview settings and input recipes are collapsed by default. Deployment and
preview require the current successful compilation, compatible firmware and
writable device storage. Program names use 1–32 letters, digits, dots, underscores
or hyphens; `_preview` is reserved. Referenced device programs remain protected
by firmware's catalogue and revision rules.

There is no browser source library, version store, directory picker or filesystem
integration. Editing is session-only; save a file before closing or refreshing.
The previous experimental browser draft/library is no longer read or migrated.
Re-import original Python files (or copy source out of an old JSON backup), then
save new source files. Device storage does not need wiping for this UI change.

## Configuration

**Presets** creates or edits complete device recipes from deployed animations.
Add up to the controller's layer limit, reorder bottom-to-top, set each layer's
opacity, blend, initialization settings and input bindings, and choose the preset
transition and duration. The same transition is used for selection, ON, OFF and
startup. **Save preset to device** preserves current playback. Opening an existing
preset reads its catalogue revision; conflicting saves retain the draft for review.

**Output & startup** configures physical outputs, pins, pixel counts, brightness,
Home Assistant bindings and power-on behavior: Off, Restore last state, or Selected
preset. Validate has no hardware side effect; Apply in RAM is temporary; Apply &
save persists across reboot. Startup-only changes preserve current playback.

**Wi-Fi, HTTP & MQTT** configures station networks or an access point, HTTP service,
and the MQTT broker. It appears according to firmware capabilities. Passwords stay
in the form and are cleared after attempted saves. A blank existing password keeps
its saved value; explicit open-network/clear-password controls remove it. Background
polling retains focused fields and unsaved drafts. See [networking](networking.md)
and [MQTT](mqtt.md).

## JavaScript and blocks

Editor now has Python, JavaScript and Blocks modes with separate session drafts.
All modes share code checks, settings, inputs, device preview and deployment.
Import/export supports `.py`, `.js`, `.blocks.json` and complete `.lightvm.json`
source documents. Blocks compile through their own frontend into the shared
LightVM compiler. See [JavaScript and blocks](javascript-and-blocks.md) for the
language contracts, examples and visual toolbox.

## Mixer

The performance desk has A and B preset buses, coloured tally lights and a central
crossfader. Cue a preset with its bus button while the other bus is visible; both
compositions continue running. The bus meters show mix position, not measured
brightness (a wipe's pixel coverage differs from a crossfade).

- Select a deployed **Transition program**, adjust settings, then **Apply transition**.
  This preserves both compositions, progress and the current driver.
- **Use fader** takes over at the current position. Drag or use arrow keys to mix
  from A (0%) to B (100%). **Hold** retains the current position.
- **Cut to A/B** jumps immediately. **Run to A/B** uses the fade duration in the AUTO
  section. **Repeat A ↔ B** is optional timed playback that continues on the device.
- **Swap A and B** is available at either endpoint, preserving the visible output.
- **On** recalls the selected/default power preset; **Fade to black** uses the
  selected preset's OFF transition. Presets and their power transitions belong in
  Configuration.

The collapsed **Deployed animations** drawer also loads a single animation with its
program defaults, using buttons or drag-and-drop. Live input controls are available
in a separate drawer. Opening Mixer never takes over an existing driver. Leaving,
changing outputs or disconnecting drops unsent fader values and retains the last
accepted position. Return and choose Use fader explicitly.

Fader updates use `desktop.fader` and its assignment token, at most every 33 ms,
with one request in flight and only the latest pending position. Another client's
assignment releases control without automatically reclaiming it.

Mixer also provides standard **Light** controls. Brightness dims the entire
output, including music and paused frames. Colour is retained even if the current
animation does not follow it. Live inputs and saved recipes provide **What controls
this?** choices for light colour, fixed values, timers, music and named sources;
JSON remains available under Advanced.

## Audio Playground

The audio desk shows six generic music meters and sensitivity/noise-floor/release
controls. **Start listening** analyzes locally; **Send music to controller**
publishes reusable signals. Choose Music → Level, Bass, etc. in each animation's
**What controls this?** selector, with response ranges or a colour palette.
The adapter does not change animation bindings. Stopping uses each input's fallback.
Listening continues across workspace routes with a visible stop control and stops
when the browser tab is hidden. See the [audio guide](audio-source.md).

## Connections

Connect over HTTP/WebSocket or USB from **Controllers**. HTTP discovery uses
`lightvm.local`; entering a direct address also works. A controller can have both
USB and network paths attached. Commands stay pinned to the selected path and are
never automatically replayed on another one. Web Serial requires HTTPS or localhost
and a supported desktop browser. Network sessions negotiate WebSocket when supported,
checking controller and boot identities before switching from bootstrap HTTP.
Disconnecting leaves autonomous playback running. After a reboot, reconnect and
review state. See [WebSocket](websocket.md).

## Run locally

From the repository root, with Node 22 and Python 3 installed:

    npm ci
    python3 tools/serve_editor.py

Open **http://localhost:8000** in desktop Chrome or Edge. The server binds to
loopback. Use the server's --port option to choose another port. npm run dev starts
the Vite development server on loopback for frontend work. The Python server runs
the Vite build before serving build/web; it has no compilation or controller backend.

The first visit to Editor loads pinned Pyodide and CodeMirror assets from the
same server as the app. Serve the complete local build to work without internet.
Source is checked 220 ms after an edit. Only
the current successful compilation can be uploaded or downloaded. Errors link
back to the affected line. The canonical examples and compiler
sources are packaged with the static build. Source is parsed and never run by the
compiler. Only the compiled bytecode is sent to a controller.

The Firmware page uses ESP Web Tools 10.4.0 from its CDN and a local manifest and
firmware bundle. The published editor builds matching Wi-Fi, HTTP, WebSocket and boot
sequence firmware for all three XIAO boards and includes the bootloader,
partition table and application binaries. Confirm that the physical board is a
supported XIAO model; the installer detects the ESP chip family only. Choose the
serial port when prompted. Existing boards should keep their data
when offered an erase choice. The published bundle also contains a USB Pico
image and Wi-Fi/HTTP/WebSocket Pico W and Pico 2 W images, all with boot-sequence support.
Pico 2 W uses the UF2 download: hold BOOTSEL while connecting, then copy its UF2
to the **RP2350** drive. Direct WebUSB installation is limited to Pico/Pico W. For Pico/Pico W, hold
BOOTSEL while connecting, select its printed model, and choose RP2 Boot
in the WebUSB picker. The RP2040 bootloader cannot distinguish Pico from Pico W.
The installer validates the UF2 and checksum, writes below the reserved flash
storage, reads back the written image and then restarts the board. A UF2 download
provides a manual fallback. Disconnect an editor USB session before flashing,
then reconnect after the board restarts. Firmware images are absent from a plain
`npm run dev` or `npm run build` unless they have been packaged first.

To include local builds in the editor, package the matching ESP-IDF outputs and
Pico UF2s before starting Vite or building the site:

    python3 tools/package_web_firmware.py --build-id local \
      --board xiao_esp32c6=build/firmware/xiao_esp32c6 \
      --pico pico=build/boot-sequence-pico/lightvm.uf2 \
      --pico pico_w=build/firmware/pico_w/lightvm.uf2 \
      --pico pico2_w=build/firmware/pico2_w/lightvm.uf2
    npm run dev

The packager checks the chip and flash offsets in `flasher_args.json` and requires
Wi-Fi, HTTP, WebSocket and boot-sequence support in the ESP build's `CMakeCache.txt` before
copying images into `build/editor-assets/firmware`. Add a `--board` argument
for each additional built XIAO profile. Each `--pico` UF2 is checked against its board/chip, contiguous application
blocks and the linked reserved flash boundary. Pico 2 W accepts only RP2350 Arm
Secure images, with strict validation of picotool's ignored RP2350-E10 workaround
block. The Pages workflow builds all six profiles and packages them into the deployed editor automatically.

For initial setup or standalone use, Wi-Fi firmware can host an access point
at **http://192.168.4.1**. Join it and open that address to provision station Wi-Fi
using the board's plain HTML form; no editor or internet access is needed. After
saving, rejoin your normal Wi-Fi and open the editor. To use the full editor while
remaining on the AP, load it before joining or serve this local build.
Select **Host an access point** in **Wi-Fi & HTTP** to
save AP settings; station and AP drafts are retained when switching modes.
Passwords remain only in the form and are cleared after attempted saves.
The controller serves the full API, while editor/compiler assets remain separate;
serve the complete local editor build when compiling without internet.
During power-cycle recovery, the panel shows saved network settings while the
open recovery AP stays active. **Save network for next boot** preserves the
current connection; restart after the detector has cleared its counter to apply
those values. Startup playback stays off during recovery; manual activation remains available.
See [AP setup and networking](networking.md#initial-setup-and-standalone-ap-mode)
and [power-cycle recovery](boot-sequence.md#recovery-access-point).

## Build and publish

Install Node 22.13+, Python 3.12+ and Emscripten 4.0.7, then run `npm ci`.
Set `EMCC` to its `emcc` executable if it is not on PATH. The prebuild compiles
the real C VM for local previews. Pinned Pyodide and CodeMirror assets ship in
the output so lessons do not depend on a CDN. See [local setup](teaching.md#building-an-editor-that-works-without-internet).

    npm run build
    python3 tools/build_editor.py --output /tmp/lightvm-web

The Vite output is build/web, with relative asset paths for GitHub Pages under
/lightvm. The build packages the worker, canonical LightVM compiler modules and both additional language frontends
and examples. The [Pages workflow](../.github/workflows/pages.yml) installs
locked npm dependencies and publishes this directory on public main pushes
after Pages is enabled with GitHub Actions as its source.

## Verification

    npm run build
    npm test
    node --test tests/test_editor.mjs

The protocol suite covers framing, discovery, request identities, revision
conflicts and a real host upload/play cycle. The browser acceptance check uses a
host simulator plus Chrome or Chromium and exercises the controller picker, all four
workspaces, external file round trips, device presets, deployments and live mixing. Set LIGHTVM_RECEIVER
to a host build and CHROME_EXECUTABLE if needed, then run npm run test:browser.
Use a host build with `LIGHTVM_HTTP=ON` and `LIGHTVM_WEBSOCKET=ON` to also check
socket negotiation and fader commands over the selected transport. It requires the pinned browser compiler assets.

### Firmware acceptance

The Pages workflow builds the supported board profiles and packages images from
one source tree. The ESP32 packager requires the correct chip, flash layout and
Wi-Fi/HTTP/WebSocket/boot-sequence feature flags. Pico packaging validates every UF2 block
and its reserved-storage boundary. The browser uses the vendored pico⚡flash
PICOBOOT implementation for RP2040 boards.

Browser rendering and simulator checks do not establish physical flashing,
readback, wireless behavior or power-recovery timing. Validate these separately
on each board when a hardware test is explicitly requested. Preserve reserved
storage and retain source/configuration backups across contract cutovers.

## Home Assistant outputs

Under **Configuration → Output & startup**, choose **Home Assistant default preset**
to expose that output. Check its **Home Assistant presets** and optionally set a
name. Apply in RAM or save the configuration for reboot. Enable MQTT separately
in **Wi-Fi, HTTP & MQTT**. Each exposed output appears in HA as one light
with power, master brightness, colour and preset effects; selecting an effect starts its complete composition.
