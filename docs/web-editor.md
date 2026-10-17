# Browser device workspace

The browser app is a static React, TypeScript and Redux Toolkit application built
with Vite. **Devices** is the entry point. Connect to a LightVM board over HTTP or
USB, then use the **Develop** and **Configure** views for that device. **Firmware**
installs and updates XIAO ESP32-C3, S3 and C6 through Web Serial, and Pico and
Pico W from BOOTSEL through WebUSB. The Pico UF2s are also available to download.
The app can also open Develop without a device for editing and bytecode compilation.

The app reuses the existing JavaScript protocol, transport and discovery modules.
A persistent device service owns USB/HTTP sessions and pins each operation to its
selected command path. Redux holds serializable device snapshots, the editor draft
and operation state. Connection and worker objects stay outside the store. Wi-Fi
passwords remain in the network form and are cleared after an attempted save.

## Run locally

From the repository root, with Node 22 and Python 3 installed:

    npm ci
    python3 tools/serve_editor.py

Open **http://localhost:8000** in desktop Chrome or Edge. The server binds to
loopback. Use the server's --port option to choose another port. npm run dev starts
the Vite development server on loopback for frontend work. The Python server runs
the Vite build before serving build/web; it has no compilation or device backend.

The first visit to Develop loads the pinned Pyodide assets, and the
CodeMirror editor uses a pinned CDN asset. Internet access is initially needed;
offline operation is not guaranteed. Source is checked 220 ms after an edit. Only
the current successful compilation can be uploaded or downloaded. Errors link
back to the affected line. The canonical examples and compiler
sources are packaged with the static build. Python source is parsed and never run by the
compiler. Only the compiled bytecode is sent to a device.

The Firmware page uses ESP Web Tools 10.4.0 from its CDN and a local manifest and
firmware bundle. The published editor builds matching Wi-Fi, HTTP and boot
sequence firmware for all three XIAO boards and includes the bootloader,
partition table and application binaries. Confirm that the physical board is a
supported XIAO model; the installer detects the ESP chip family only. Choose the
serial port when prompted. Existing boards should keep their data
when offered an erase choice. The published bundle also contains a USB Pico
image and a Wi-Fi/HTTP Pico W image, both with boot-sequence support. Hold
BOOTSEL while connecting a Pico, select its printed model, and choose RP2 Boot
in the WebUSB picker. The RP2040 bootloader cannot distinguish Pico from Pico W.
The installer validates the UF2 and checksum, writes below the reserved flash
storage, reads back the written image and then restarts the board. A UF2 download
provides a manual fallback. Disconnect an editor USB session before flashing,
then reconnect after the board restarts. Firmware images are absent from a plain
`npm run dev` or `npm run build` unless they have been packaged first.

To include local builds in the editor, package the matching ESP-IDF outputs and
Pico UF2s before starting Vite or building the site:

    python3 tools/package_web_firmware.py --version local \
      --board xiao_esp32c6=build/boot-sequence-c6 \
      --pico pico=build/boot-sequence-pico/lightvm.uf2 \
      --pico pico_w=build/boot-sequence-pico-w-http/lightvm.uf2
    npm run dev

The packager checks the chip and flash offsets in `flasher_args.json` and requires
Wi-Fi, HTTP and boot-sequence support in the ESP build's `CMakeCache.txt` before
copying images into `build/editor-assets/firmware`. Add a `--board` argument
for each additional built XIAO profile. Each `--pico` UF2 is checked for RP2040
family, contiguous blocks and the reserved flash boundary. The Pages workflow
builds all five profiles and packages them into the deployed editor automatically.

For initial setup or standalone use, Wi-Fi firmware can host an access point
at **http://192.168.4.1**. Open the hosted editor before joining that network or
serve this local build. Select **Host an access point** in **Wi-Fi & HTTP** to
save AP settings; station and AP drafts are retained when switching modes.
Passwords remain only in the form and are cleared after attempted saves.
The device serves the full API, while editor/compiler assets remain separate;
load Develop while online before relying on compilation without internet.
During power-cycle recovery, the panel shows saved network settings while the
open recovery AP stays active. **Save network for next boot** preserves the
current connection; restart after the detector has cleared its counter to apply
those values. Startup programs stay idle during recovery; manual activation remains available.
See [AP setup and networking](networking.md#initial-setup-and-standalone-ap-mode)
and [power-cycle recovery](boot-sequence.md#recovery-access-point).

## Device and development flow

**Devices** tries the lightvm.local HTTP discovery endpoint. Pick a nearby board,
enter a direct HTTP address, or choose a USB port in the browser. HTTP can connect
without USB. Discovery is read-only, and local-network browser permission may be
needed. A USB-only board may need its network settings provisioned over USB first.
See [networking](networking.md#local-device-discovery) for details.

After connecting, the workspace shows the board, output binding and current
command path. USB and HTTP can be attached to the same device and boot epoch.
Switching paths does not replay commands through another connection. Disconnecting
the browser leaves autonomous playback running. If the device reboots, reconnect
and review its state. Web Serial requires HTTPS or localhost and desktop
Chrome/Edge. Editing and bytecode downloads still work in browsers without Web Serial.

In **Configure → Output & startup**, select the data pin, count, colour order and
brightness limit for each output, and optionally select its stored startup program
and arguments. Add or remove outputs independently. Startup arguments are a JSON
object; omitted values use program defaults. Changing a program clears its argument
draft, and choosing idle removes that output’s startup invocation. An advanced
JSON editor stays synchronized with the form.
Validate has no hardware side effect; Apply changes RAM; Apply & save persists for
reboot. Unchanged hardware retains playback, including after startup-only edits. Saves use the revision
loaded with the draft. If another client changes device settings, the draft stays
intact and saving pauses until the new state is reviewed.

**Configure → Wi-Fi & HTTP** appears only on network-capable firmware. It edits Wi-Fi
enablement, country and the ordered SSID list, plus HTTP service/port settings.
An existing blank password retains the saved password; selecting an open network
clears it. Network changes keep playback running and may require the HTTP path to
reconnect. Background status polling does not replace focused fields or unsaved
values.

**Develop** contains the Python editor, upload/run, stored bytecode programs,
initialization settings and playback controls. The initial `rainbow.py` is selected in
the dropdown and uses the same canonical source as its example. It preserves the
original picolights Rainbow's default 60-second cycle, adjustable with its existing
duration setting. Existing stored programs are not replaced when firmware or
examples change. Sweep supports moving eyes, retained chase/wipe and sampled
trails through its initialization settings; wipe is one of its documented modes.
Each example's opening comments describe the available modes and settings;
see also [motion](motion.md#shared-sweep-animation).
Animations have no automatic stop
condition. The local draft is saved in browser storage and can be downloaded as
.py. Compiled .lbc can also be downloaded. **Upload & run** requires slot A to
be configured; it replaces the transient resident program and does not persist it.
Stored programs can be saved, downloaded, activated and deleted when storage is
available. The playback panel sends generation checks for pause/resume. Live parameter
controls remain available only for stored ABI 2 programs.

The [animation/1](animation-profile.md) initialization settings panel validates and
restarts a fresh instance, preserving the prior instance if preparation fails.
Its changes are runtime-only: edit source defaults and save the compiled program
to retain them after reboot. Playback status refreshes every five seconds without
overwriting unsaved initialization settings. Settings are frozen for each instance;
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
host simulator plus Chrome or Chromium and exercises the device picker, both
workspace views, configurations, uploads and live controls. Set LIGHTVM_RECEIVER
to a host build and CHROME_EXECUTABLE if needed, then run npm run test:browser.
It requires the pinned browser compiler assets.

### XIAO ESP32-C6 browser installation, 13 October 2026

A newly connected 4 MiB ESP32-C6 was identified over USB Serial/JTAG. The
local Firmware page served the XIAO C6 Wi-Fi/HTTP/boot-sequence bundle and the
Chrome flow reached the new LightVM device workspace. Readback verification
matched the packaged bootloader at `0x0`, partition table at `0x8000` and app
at `0x10000` against the device flash. After reset, USB `describe` reported
`board:xiao_esp32c6`, Control ABI 2, Wi-Fi/HTTP and recovery support, with
empty boot and animation errors. `status` returned idle with no output
configuration, as expected for a new installation.

### XIAO ESP32-S3 browser installation, 13 October 2026

The next connected USB Serial/JTAG device identified as ESP32-S3 with 8 MiB
flash. The first local installer attempt said the chip was unsupported because
that local manifest contained only the C6 image. Repackaging the local site
with all three XIAO images and reloading the page exposed the S3 bundle. The
browser installation then reached the LightVM workspace, which connected over
USB as `xiao_esp32s3` with device ID matching the detected chip MAC and fresh
output setup. The S3 flash bytes were not independently read back while the
browser held the active USB session.

### XIAO ESP32-C3 browser installation, 13 October 2026

The first local C3 installer bundle accidentally used a USB-only profile, so
the device reported no Wi-Fi/HTTP support and the editor hid its network
configuration. The bundle was corrected to the C3 Wi-Fi/HTTP/boot-sequence
profile, and the packager now rejects installer images built without those
features. Kaj installed the corrected image through the Chrome installer. The
editor then showed Wi-Fi & HTTP configuration, a connected Wi-Fi network, and
both USB and HTTP command paths for the C3. Flash bytes were not independently
read back after installation.

### Pico and Pico W browser installation

The Pages workflow builds and validates RP2040 UF2 images for Pico and Pico W.
The browser path uses the vendored pico⚡flash PICOBOOT implementation. Local
packaging and browser rendering have been checked with real UF2 builds; direct
WebUSB flashing and readback still need a physical Pico and Pico W test.
