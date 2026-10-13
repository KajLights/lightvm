# Browser device workspace

The browser app is a static React, TypeScript and Redux Toolkit application built
with Vite. **Devices** is the entry point. Connect to a LightVM board over HTTP or
USB, then use the **Develop** and **Configure** views for that device. **Firmware**
has a visible route for installation and updates; flashing is not implemented yet.
The app can also open Develop without a device for editing and WASM compilation.

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

The first visit to Develop loads the pinned Pyodide and WABT assets, and the
CodeMirror editor uses a pinned CDN asset. Internet access is initially needed;
offline operation is not guaranteed. Source is checked 220 ms after an edit. Only
the current successful compilation can be uploaded or downloaded. Errors link
back to the affected line. The six examples and the canonical compiler sources
are packaged with the static build. Python source is parsed and never run by the
compiler. Only the compiled WASM is sent to a device.

For initial setup or standalone use, Wi-Fi firmware can host an access point
at **http://192.168.4.1**. Open the hosted editor before joining that network or
serve this local build. Select **Host an access point** in **Wi-Fi & HTTP** to
save AP settings; station and AP drafts are retained when switching modes.
Passwords remain only in the form and are cleared after attempted saves.
The device serves the full API, while editor/compiler assets remain separate;
load Develop while online before relying on compilation without internet.
See [AP setup and networking](networking.md#initial-setup-and-standalone-ap-mode).

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
Chrome/Edge. Editing and WASM downloads still work in browsers without Web Serial.

In **Configure → Output & startup**, select the data pin, count, colour order and
brightness limit, bind the output to slot A, and optionally select a stored startup
program. An advanced JSON editor is available for the full configuration schema.
Validate has no hardware side effect; Apply changes RAM; Apply & save persists for
reboot. Applying output or startup settings stops playback. Saves use the revision
loaded with the draft. If another client changes device settings, the draft stays
intact and saving pauses until the new state is reviewed.

**Configure → Wi-Fi & HTTP** appears only on network-capable firmware. It edits Wi-Fi
enablement, country and the ordered SSID list, plus HTTP service/port settings.
An existing blank password retains the saved password; selecting an open network
clears it. Network changes keep playback running and may require the HTTP path to
reconnect. Background status polling does not replace focused fields or unsaved
values.

**Develop** contains the Python editor, upload/run, stored WASM programs and live
parameters. The local draft is saved in browser storage and can be downloaded as
.py. Compiled .wasm can also be downloaded. **Upload & run** requires slot A to
be configured; it replaces the transient resident program and does not persist it.
Stored programs can be saved, downloaded, activated and deleted when storage is
available. The parameter panel uses the program's manifest and sends generation
and revision checks for live changes and pause/resume. A conflict refreshes device
state before another edit.

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
