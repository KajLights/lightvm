# Static Python editor

The editor checks and compiles the existing restricted Python language locally in
a Pyodide worker. WABT assembles the generated WebAssembly text. Only the compiled
WASM is sent to a device; user source is parsed and never executed by the compiler.
The HTTP server serves static files and has no compilation or device backend.

## Run locally

From the repository root:

```sh
python3 tools/serve_editor.py
```

Open **http://localhost:8000** in desktop Chrome or Edge. Use `--port 8080` to
choose another port. The server binds to loopback only. Close it with Ctrl+C.
Internet access is initially needed for the pinned Pyodide, WABT and CodeMirror
assets. Offline operation is not guaranteed by this prototype.

Start typing while Python loads. After initialization, syntax/scope checks and
restricted-language compilation run 220 ms after an edit. Errors appear below the
editor and at the affected line, with an underline and gutter marker. Clicking an
error moves the cursor there. Results from old edits are discarded immediately;
upload/download buttons only use the current successful compilation.

Load one of the six examples, open a `.py` file, or edit the initial rainbow.
The draft is saved in this browser's local storage. **Save .py** downloads editable
source; **Download .wasm** downloads the compiled binary with its program manifest.

## Connect and upload

For a provisioned Wi-Fi/HTTP board, the editor automatically fetches a device list
through `lightvm.local`. Choose a board under **Nearby devices** and click
**Connect HTTP**. **Refresh** retries the search. The list is supplied by a board,
so no desktop discovery helper is needed; browsers may request local-network
permission. A USB connection or direct HTTP address can also seed discovery.
See [networking](networking.md#local-device-discovery) for provisioning, custom
ports, discovery limits and the endpoint available to other tools.

1. Run the current LightVM firmware and connect the board normally over USB.
   Close other serial clients using the same device.
2. Configure the light strip's pin/count and bind it to slot A using the existing
   [configuration tools](configuration.md). The editor displays the current output
   binding and keeps uploading disabled when slot A is unconfigured.
3. Click **Connect device** and select the Pico or ESP32 native USB port.
   Discovery checks Control ABI 2, program schema 3 and the module size limit.
4. Click **Upload & run** after the code checks pass. **Stop lights** stops playback
   and clears the output. Disconnecting the browser leaves playback running.

Browser USB access uses **Web Serial** with the existing USB CDC firmware and
protocol v1: STATUS/discovery, BEGIN, ordered CHUNKs, FINISH and PLAY. It validates
frame CRCs, request identities, upload offsets and the device's size limit.
No new USB interface or firmware change is needed. A timeout or corrupt response
closes the connection; reconnect before another upload.

An accepted BEGIN replaces the resident program. An interrupted or rejected
upload can therefore leave the device idle; the previous resident animation is
not automatically restored. Uploading does not save a program or change startup
configuration. Use the management panels below for persistence, hardware configuration and live
parameters. Animation preview remains outside this editor.

Pico connections assert DTR at 115200 baud. ESP32 connections deassert DTR/RTS
immediately after opening. Web Serial cannot set those signals before opening,
so some host/browser combinations may reset an ESP32 on connection. Browser
uploads have been checked on the original Pico; ESP32 hardware validation remains
pending.

Web Serial requires HTTPS or a trusted loopback origin, and is unavailable in
Safari/Firefox. Unsupported browsers can still edit and download compiled programs.
Opening `index.html` directly through `file://` is unsupported.


## HTTP and management panels

[Build and provision networking](networking.md) once through USB. Enter the device's
HTTP origin and click **Connect HTTP**. USB and HTTP can remain connected; the
**Command path** selector chooses the path for subsequent operations. Each
operation stays on its selected path, and failures never replay it through another
connection. Device and boot identity must match before attaching the second path.
Closing a path leaves autonomous playback running.

Background polling updates connection/network status in place. It preserves form
focus, text selection and unsaved settings, and does not replace the command-path
selector or disable controls. A manually entered HTTP address is retained.

The four management panels provide:

- Wi-Fi enablement, country, an ordered SSID list, HTTP settings and connection
  status. Existing blank passwords are retained; selecting an open network clears
  its password. Credentials are cleared from the form after saving and never
  written to browser storage.
- Output JSON validation, RAM apply, persistent apply/save and startup selection.
  Applying output configuration stops playback; network changes preserve it.
- Named compiled WASM save, list, download, deletion and activation on A. Saving
  does not activate or change startup. Python source remains in browser drafts and
  downloads.
- Manifest-derived bool, int, float and RGB8 controls, plus pause/resume. Parameter
  changes use the displayed generation/revision and refresh after a conflict.

Capabilities determine which controls are enabled. Network controls require a
network-enabled firmware; storage controls require available device storage.
**Upload & run** continues to replace a transient resident program independently
of library saves.

HTTP works without Web Serial. Modern Chrome/Edge can request local-network access
from an HTTPS editor; grant that permission when requested. The client annotates
local requests, supports CORS and shows reachability/permission errors. Browser
support varies: use the localhost editor when HTTPS-to-HTTP access is blocked.
See [Chrome's local-network access guidance](https://developer.chrome.com/blog/local-network-access).
No browser security flags or automatic path failover are required.

## Host elsewhere

```sh
python3 tools/build_editor.py
```

Serve the contents of `build/web/` from any static HTTPS host. The packaging tool
copies `web/`, the three canonical `lightvm` Python files and the six examples;
there is no duplicate compiler source to maintain and no JavaScript build step.
Run it again after changing source assets or the compiler. An alternate output
directory can be selected with `--output PATH`.

Dependencies are pinned to Pyodide **314.0.7** (MPL-2.0), WABT **1.0.39**
(Apache-2.0) and CodeMirror **5.65.18** (MIT), loaded from their CDN distributions.

### GitHub Pages

The [Browser editor Pages workflow](../.github/workflows/pages.yml) packages the
editor on pushes and pull requests to `main`. When the repository is public,
pushes to `main` also deploy it to **https://kajlights.github.io/lightvm/**.
Pull requests and private repositories build the package without publishing it.
Only `build/web/` is uploaded, including the browser compiler and examples.
Asset paths are relative, so the editor works under the `/lightvm/` project path.

To enable hosting after making the repository public:

1. Open **Settings → Pages** in `KajLights/lightvm`.
2. Under **Build and deployment**, set **Source** to **GitHub Actions**.
3. Push to `main`, or open **Actions → Browser editor Pages → Run workflow**
   and select `main` to publish the current version.

The workflow's `github-pages` environment reports the deployed URL. Subsequent
pushes to `main` update the editor automatically. Deployment uses the standard
GitHub token and requires no additional secrets or compilation server.
HTTPS supports Web Serial in desktop Chrome/Edge; local HTTP devices can still
require local-network permission as described above. CDN access is required to
load the editor dependencies.

## Verification

```sh
node --test tests/test_editor.mjs
build/venv/bin/python -m unittest discover -s tests -p test_control.py -v
```

The Node checks compare framing with Python/zlib, fragmented/noisy input, bad
CRCs, timeout recovery and a real upload/play/stop cycle against `build/host/lightvm`.
The host test requires that executable and the project's existing Python venv;
it skips when they are unavailable. Set `LIGHTVM_RECEIVER` to check another host
profile. The Python checks cover the injected browser assembler and existing
compiler/control semantics.

On 12 October 2026, Chrome browser QA passed inline syntax diagnostics, stale
artifact invalidation, all six example compilations/downloads and desktop/mobile
layout. All six browser-generated binaries matched trusted Python replay when
stepped by the real wasm3 host. Connect/upload/run/stop/disconnect/reconnect passed
with browser serial streams bridged to that host. The 22 Python control tests and
four Node checks passed. After updating an original Pico to Control ABI 2, Chrome
connected through Web Serial and reported successful uploads/playback of Rainbow,
Twinkle, Pattern and Wipe with its existing 200-pixel GP16 configuration.
ESP32 browser uploads remain unverified.
