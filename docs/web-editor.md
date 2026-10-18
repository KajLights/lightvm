# Browser controller workspace

The browser app is a static React, TypeScript and Redux Toolkit application built
with Vite. **Controllers** is the entry point. Connect to a LightVM board over HTTP or
USB, then use the **Develop**, **Fader** and **Configure** views for that controller. **Firmware**
installs and updates XIAO ESP32-C3, S3 and C6 through Web Serial, and Pico and
Pico W from BOOTSEL through WebUSB. The Pico UF2s are also available to download.
The app can also open Develop without a controller for editing and bytecode compilation.

**Audio demo** captures microphone or line-in audio, shows its response locally,
and can drive the running Eye animation through live inputs. See the
[audio Eye demo guide](audio-eye-demo.md) for setup and mappings.

The app reuses the existing JavaScript protocol, transport and discovery modules.
A persistent controller service owns USB/HTTP sessions and pins each operation to its
selected command path. Redux holds serializable controller snapshots, the editor draft
and operation state. Connection and worker objects stay outside the store. Wi-Fi
passwords remain in the network form and are cleared after an attempted save.

## Fader and timed transitions

Open the controller workspace's **Fader** tab and select the output. Drag an animation
from **Saved animations** onto **Composition A** or **Composition B**, or use that animation's
**Load A** / **Load B** buttons. The list includes browser animation versions and
animations already stored on the controller. Browser versions use their saved
initialization settings and upload their compiled program only when missing;
controller-only programs use their defaults. Saved transition programs are excluded.

Loading replaces that slot with one animation while preserving the other composition,
transition percentage and current manual or timed driver. Failed preparation
retains the running compositions. For layered compositions, prepare either slot in Develop's
A/B panel. Both compositions keep running. Opening the tab does not interrupt an
existing timed transition.

Choose a deployed program under **Transition program**, adjust its exposed
settings if needed, then **Apply transition**. The list comes from transitions
stored on the connected controller, including any separately named variants.
Unchecked settings use the program defaults; colour settings have a colour picker.
Applying retains both compositions, the percentage and the current fader or timed driver.
The same selector is available in Develop's A/B panel.

For the wipe example, leave `separator` at its default for a plain wipe. Override
`separator` to True for the three-pixel separator, and override `color` to choose
its colour (white by default). `reverse` changes the reveal direction. Compile
and save the updated wipe example in Develop to deploy these settings; an
already stored older wipe exposes only the settings it was compiled with.

**Use fader** takes control at the current percentage. Move the slider from A at
0% to B at 100%, using the pointer or keyboard. The browser supplies an external
input named `desktop.fader` through the selected USB or network connection. Updates
use the assignment token returned by the controller. The browser sends at most
one update every 33 ms, with one request in flight and only the latest pending
position, so slow connections do not build a backlog of obsolete positions.

**Hold** releases the driver and retains its current position. **Run to A** and
**Run to B** start a timed fade from the current position. **Start timed demo**
starts at A and alternates between A and B, using the configured fade duration
and repeat interval. The interval includes the fade and must be at least its
duration. Timed playback runs on the controller and continues after navigation
or disconnection.

Starting a timed movement releases the fader. If another controller replaces its
assignment, this slider releases control and does not reclaim it automatically.
Leaving the Fader tab, changing outputs or disconnecting discards unsent slider
values; the last accepted position remains. Return and choose **Use fader** to
take control again. This input controls transition progress only; live animation
variable bindings remain future work.

## Run locally

From the repository root, with Node 22 and Python 3 installed:

    npm ci
    python3 tools/serve_editor.py

Open **http://localhost:8000** in desktop Chrome or Edge. The server binds to
loopback. Use the server's --port option to choose another port. npm run dev starts
the Vite development server on loopback for frontend work. The Python server runs
the Vite build before serving build/web; it has no compilation or controller backend.

The first visit to Develop loads the pinned Pyodide assets, and the
CodeMirror editor uses a pinned CDN asset. Internet access is initially needed;
offline operation is not guaranteed. Source is checked 220 ms after an edit. Only
the current successful compilation can be uploaded or downloaded. Errors link
back to the affected line. The canonical examples and compiler
sources are packaged with the static build. Python source is parsed and never run by the
compiler. Only the compiled bytecode is sent to a controller.

The Firmware page uses ESP Web Tools 10.4.0 from its CDN and a local manifest and
firmware bundle. The published editor builds matching Wi-Fi, HTTP, WebSocket and boot
sequence firmware for all three XIAO boards and includes the bootloader,
partition table and application binaries. Confirm that the physical board is a
supported XIAO model; the installer detects the ESP chip family only. Choose the
serial port when prompted. Existing boards should keep their data
when offered an erase choice. The published bundle also contains a USB Pico
image and a Wi-Fi/HTTP/WebSocket Pico W image, both with boot-sequence support. Hold
BOOTSEL while connecting a Pico, select its printed model, and choose RP2 Boot
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
      --pico pico_w=build/firmware/pico_w/lightvm.uf2
    npm run dev

The packager checks the chip and flash offsets in `flasher_args.json` and requires
Wi-Fi, HTTP, WebSocket and boot-sequence support in the ESP build's `CMakeCache.txt` before
copying images into `build/editor-assets/firmware`. Add a `--board` argument
for each additional built XIAO profile. Each `--pico` UF2 is checked for RP2040
family, contiguous blocks and the reserved flash boundary. The Pages workflow
builds all five profiles and packages them into the deployed editor automatically.

For initial setup or standalone use, Wi-Fi firmware can host an access point
at **http://192.168.4.1**. Open the hosted editor before joining that network or
serve this local build. Select **Host an access point** in **Wi-Fi & HTTP** to
save AP settings; station and AP drafts are retained when switching modes.
Passwords remain only in the form and are cleared after attempted saves.
The controller serves the full API, while editor/compiler assets remain separate;
load Develop while online before relying on compilation without internet.
During power-cycle recovery, the panel shows saved network settings while the
open recovery AP stays active. **Save network for next boot** preserves the
current connection; restart after the detector has cleared its counter to apply
those values. Startup programs stay idle during recovery; manual activation remains available.
See [AP setup and networking](networking.md#initial-setup-and-standalone-ap-mode)
and [power-cycle recovery](boot-sequence.md#recovery-access-point).

## Controller and development flow

**Controllers** tries the lightvm.local HTTP discovery endpoint. Pick a nearby board,
enter a direct HTTP address, or choose a USB port in the browser. HTTP can connect
without USB. Discovery is read-only, and local-network browser permission may be
needed. A USB-only board may need its network settings provisioned over USB first.
See [networking](networking.md#local-controller-discovery) for details.

Network connections identify the controller over HTTP and automatically select
[WebSocket](websocket.md) when the firmware advertises support. The controller ID and
boot ID must match on both transports before the editor attaches the connection.
The bootstrap HTTP session is then closed; fader updates, uploads and other
commands use the persistent socket. The connection selector and status display
show **WebSocket** or **HTTP**. Firmware without WebSocket support continues over
HTTP. An advertised WebSocket that fails to connect reports an error. A lost
socket disconnects its path; commands are never automatically replayed or moved
to HTTP or USB.

After connecting, the workspace shows the board, output binding and current
command path. USB and a network path can be attached to the same controller and boot epoch.
Switching paths does not replay commands through another connection. Disconnecting
the browser leaves autonomous playback running. If the controller reboots, reconnect
and review its state. Web Serial requires HTTPS or localhost and desktop
Chrome/Edge. Editing and bytecode downloads still work in browsers without Web Serial.

In **Configure → Output & startup**, select the data pin, count, colour order and
brightness limit for each output, and optionally select its stored startup program
and settings. Add or remove outputs independently. Startup settings are a JSON
object; omitted values use program defaults. Changing a program clears its settings
draft, and choosing idle removes that output’s startup invocation. An advanced
JSON editor stays synchronized with the form.
Validate has no hardware side effect; Apply changes RAM; Apply & save persists for
reboot. Unchanged hardware retains playback, including after startup-only edits. Saves use the revision
loaded with the draft. If another client changes controller settings, the draft stays
intact and saving pauses until the new state is reviewed.

**Configure → Wi-Fi & HTTP** appears only on network-capable firmware. It edits Wi-Fi
enablement, country and the ordered SSID list, plus HTTP service/port settings.
An existing blank password retains the saved password; selecting an open network
clears it. Network changes keep playback running and may require the HTTP path to
reconnect. Background status polling does not replace focused fields or unsaved
values.

**Develop** follows **Edit → Preview → Save animation → Compose (optional) → Save
preset**. Select a physical output for preview; preview replaces its playback and
leaves the other outputs running. There is no in-browser LED simulation yet.
The initial `rainbow.py` is selected in
the dropdown and uses the same canonical source as its example. It preserves the
original picolights Rainbow's default 60-second cycle, adjustable with its existing
duration setting. Existing stored programs are not replaced when firmware or
examples change. Sweep supports moving eyes, retained chase/wipe and sampled
trails through its initialization settings; wipe is one of its documented modes.
Each example's opening comments describe the available modes and settings;
see also [motion](motion.md#shared-sweep-animation).
Animations have no automatic stop condition. The current source draft is saved
automatically and can be exported as `.py`; compiled `.lbc` downloads remain
available. **Preview animation** uses that draft's settings. Unchecked overrides
use the animation defaults; after a successful preview, the resolved settings
are shown. Editing settings requires another preview. Polling a running program
never replaces the draft's inputs. Firmware must advertise `transient_settings`
to preview explicit overrides atomically.

**Save animation** stores source, compiled code, selected defaults and a full
compiled SHA-256 digest in the browser library. Each save creates a version.
**Edit animation** opens that version's source and defaults. **Use alone** starts
a one-layer recipe; **Add as layer** adds an independently configured instance.
Unused animation versions and browser presets can be deleted from the library;
saved browser presets protect their referenced animation versions.
Layers are ordered bottom to top, with individual settings, opacity and `over`,
`add`, `screen` or `multiply` blending. **Preview preset** saves missing compiled
program versions on the controller, then admits and activates the entire composition.
The controller checks available storage for each upload. The displayed frame
buffer estimate excludes VM state; controller admission remains authoritative.

**Preset entry transition** chooses a deployed transition, initialization settings
and duration, and pins its program version. **Save preset** stores the recipe in the browser. **Save preset to controller** stores
the shared compiled programs and compact recipe in flash; it does not change
playback. Either save replaces a preset of the same name. **Run on output**
recalls a controller preset using its entry transition on the selected output,
even after reconnecting or rebooting. A new recall completes the previous one and
starts a fresh A-to-B movement. **Turn on** and **Turn off** use the output power
transitions configured under **Configure → Output & startup**. Manual A/B controls
retain fixed slots; **Swap A and B** is explicit and available only at endpoints. Startup remains a single stored program configured separately; saved
presets do not automatically run at boot. Controller presets require `presets: true`.
**Controller preset entry transition** also edits an existing controller recipe
without requiring its original browser library. **Save preset transition** preserves
its layers and playback; stale catalogue revisions require **Reload preset**.
Controller presets and compiled programs share available storage without fixed count
quotas. The storage panel shows free and total capacity and distinguishes animations
and transitions with the same name.

**Export library** preserves editable source, versions, settings and recipes in
one JSON file. **Import library** merges current versions and checks explicit
schema/profile/ABI, embedded package metadata, digests and references. Obsolete
artifacts require recompilation from source; their saved browser data is not erased. Browser storage has a quota and can be cleared by the
browser; export a backup before moving browsers or computers. Source is kept off
the microcontroller. Failed browser storage writes are reported, and unreadable
saved data can be downloaded with **Export preserved library** until explicit
import recovery. Libraries remain readable when a saved program requires newer
firmware; the editor explains its minimum release when previewing. Pre-baseline
artifacts remain available for source editing and export, and require recompilation
before execution. See [versioning](versioning.md). Controller program storage
controls can remove unused compiled programs; saved presets and active/saved
startup or playback references protect programs still in use.

The playback panel addresses the selected output and its first layer, with
generation checks for pause/resume. Settings are edited before preview or reset.

The [animation](animation-profile.md) settings are frozen for each instance;
there are no listeners or cycle notifications. Profile and
format capability checks prevent sending new examples to unsupported firmware.

Pico Web Serial uses the existing USB CDC protocol. Pico asserts DTR at 115200;
ESP32 deasserts DTR/RTS immediately after opening. Web Serial cannot set signals
before opening, so some ESP32 combinations may reset on connection. Pico browser
uploads have been checked on hardware; ESP32 browser hardware validation is still
pending.

## Build and publish

    npm run build
    python3 tools/build_editor.py --output /tmp/lightvm-web

The Vite output is build/web, with relative asset paths for GitHub Pages under
/lightvm. The build packages the worker, three canonical LightVM Python modules
and examples. The [Pages workflow](../.github/workflows/pages.yml) installs
locked npm dependencies and publishes this directory on public main pushes
after Pages is enabled with GitHub Actions as its source.

## Verification

    npm run build
    npm test
    node --test tests/test_editor.mjs

The protocol suite covers framing, discovery, request identities, revision
conflicts and a real host upload/play cycle. The browser acceptance check uses a
host simulator plus Chrome or Chromium and exercises the controller picker, both
workspace views, configurations, uploads and live controls. Set LIGHTVM_RECEIVER
to a host build and CHROME_EXECUTABLE if needed, then run npm run test:browser.
Use a host build with `LIGHTVM_HTTP=ON` and `LIGHTVM_WEBSOCKET=ON` to also check
socket negotiation, fader commands without HTTP requests, HTTPS local-network
access and disconnect handling. It requires the pinned browser compiler assets.

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

Under **Configure → Output & startup**, choose **Home Assistant default preset**
to expose that output. Check its **Home Assistant presets** and optionally set a
name. Apply in RAM or save the configuration for reboot. Enable MQTT separately
in **Wi-Fi, HTTP & MQTT**. Each exposed output appears in HA as an on/off light
and a Preset selector; selecting a preset starts its complete composition.
