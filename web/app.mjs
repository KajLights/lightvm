import { LightVMClient, COMMAND } from "./protocol.mjs";
import { SerialTransport, HttpTransport } from "./transports.mjs";

import { Connections } from "./connections.mjs";
import { DeviceDiscovery } from "./discovery.mjs";
const paths = new Connections();
const discovery = new DeviceDiscovery();
let nearbyDevices = [], findingDevices = false;
let parameterState = null;
const $ = id => document.getElementById(id);
const textarea = $("source"), storageKey = "lightvm.editor.draft.v1";
let editor, worker, compilerReady = false, revision = 0, artifact = null;
let client = null, target = null, outputReady = false, busy = false;
let polling = false, suggestedHttpAddress = "";
let debounce, compilationDeadline, initializationDeadline, marks = [], errorLines = [];
let fileRevision = 0;

try {
  const saved = JSON.parse(localStorage.getItem(storageKey));
  if (saved && typeof saved.source === "string") {
    textarea.value = saved.source;
    if (typeof saved.filename === "string") $("filename").textContent = saved.filename;
  }
} catch { $("draft-state").textContent = "Draft available for this session"; }

if (globalThis.CodeMirror) {
  editor = CodeMirror.fromTextArea(textarea, {
    mode: { name: "python", version: 3 }, lineNumbers: true, indentUnit: 4, tabSize: 4,
    indentWithTabs: false, autoCloseBrackets: true, matchBrackets: true,
    gutters: ["CodeMirror-linenumbers", "error-gutter"],
    extraKeys: {
      Tab: cm => cm.somethingSelected() ? cm.indentSelection("add") : cm.replaceSelection("    ", "end"),
      "Shift-Tab": cm => cm.indentSelection("subtract"),
      "Ctrl-Enter": () => $("upload").click(), "Cmd-Enter": () => $("upload").click(),
    },
  });
  editor.getInputField().setAttribute("aria-label", "Animation Python source");
  editor.on("change", changed);
  editor.on("cursorActivity", () => {
    const position = editor.getCursor();
    $("cursor-position").textContent = `Ln ${position.line + 1}, Col ${position.ch + 1}`;
  });
} else {
  textarea.addEventListener("input", changed);
  textarea.addEventListener("keydown", event => {
    if (event.key === "Tab") {
      event.preventDefault(); textarea.setRangeText("    ", textarea.selectionStart, textarea.selectionEnd, "end"); changed();
    }
  });
  log("The enhanced editor could not load. Plain text editing and Python checks are still available.");
}

const source = () => editor ? editor.getValue() : textarea.value;
function log(message, kind = "") {
  const item = document.createElement("li"), dot = document.createElement("span"), text = document.createElement("span");
  item.className = kind; dot.className = "log-dot"; text.textContent = message;
  item.append(dot, text); $("activity").prepend(item);
  while ($("activity").children.length > 40) $("activity").lastElementChild.remove();
}
function notice(message = "") { $("notice").textContent = message; $("notice").hidden = !message; }
function buttons() {
  $("connect").disabled = busy || paths.paths.has("usb") || !isSecureContext || !navigator.serial;
  $("connect").hidden = paths.paths.has("usb"); $("disconnect").hidden = !client;
  $("disconnect").disabled = busy;
  $("upload").disabled = busy || !client || !artifact || !outputReady;
  $("stop").disabled = busy || !client;
  $("download").disabled = !artifact;
  $("connect-http").disabled = busy || paths.paths.has("http");
  $("discover-devices").disabled = busy || findingDevices;
  $("nearby-devices").disabled = busy || !nearbyDevices.length || paths.paths.has("http");
  $("command-path").disabled = busy || !paths.paths.size;
  for (const id of ["network-refresh", "config-refresh", "procedures-refresh", "parameters-refresh"]) $(id).disabled = busy || !client;
  $("config-fields").disabled = busy || !client;
  $("procedure-fields").disabled = busy || !client || !target?.storage_available;
  $("procedure-save").disabled = busy || !client || !artifact || !target?.storage_available;
  $("network-fields").disabled = busy || !client || !(target?.wifi_supported || target?.http_supported) || !target?.storage_available;
  $("wifi-enabled").disabled = !target?.wifi_supported;
  $("http-enabled").disabled = !target?.http_supported;
  $("parameter-fields").disabled = busy || !client || !parameterState;

}
function clearMarks() {
  marks.forEach(mark => mark.clear()); marks = [];
  if (editor) {
    editor.clearGutter("error-gutter");
    errorLines.forEach(line => editor.removeLineClass(line, "background", "error-line"));
  }
  errorLines = [];
}
function characterOffset(line, column) { return Array.from(line).slice(0, Math.max(0, column - 1)).join("").length; }
function diagnostics(items, message) {
  clearMarks(); $("diagnostics").replaceChildren();
  if (!items.length) {
    const text = document.createElement("p"); text.textContent = message || "Code checks passed. Your animation is ready to compile and upload.";
    text.className = artifact ? "check-ok" : "muted"; $("diagnostics").append(text); return;
  }
  for (const item of items) {
    const lineNumber = Math.max(1, Math.min(item.line || 1, source().split("\n").length));
    const button = document.createElement("button"), location = document.createElement("strong"), text = document.createElement("span");
    button.className = "diagnostic"; location.textContent = `Line ${lineNumber}:${item.column || 1}`; text.textContent = item.message;
    button.append(location, text); $("diagnostics").append(button);
    if (editor) {
      const line = lineNumber - 1, content = editor.getLine(line);
      const column = characterOffset(content, item.column || 1);
      const from = { line, ch: Math.min(column, Math.max(0, content.length - 1)) };
      const end = item.end_line === lineNumber ? characterOffset(content, item.end_column || item.column + 1) : column + 1;
      const to = { line, ch: Math.min(content.length, Math.max(from.ch + 1, end)) };
      if (to.ch > from.ch) marks.push(editor.markText(from, to, { className: "code-error", title: item.message }));
      const marker = document.createElement("span"); marker.className = "error-marker"; marker.textContent = "●"; marker.title = item.message;
      editor.setGutterMarker(line, "error-gutter", marker);
      errorLines.push(editor.addLineClass(line, "background", "error-line"));
      button.onclick = () => { editor.focus(); editor.setCursor({ line, ch: column }); editor.scrollIntoView({ line, ch: column }, 60); };
    } else button.onclick = () => textarea.focus();
  }
}
function changed() {
  fileRevision++; revision++; artifact = null;
  clearTimeout(debounce); clearTimeout(compilationDeadline); clearMarks();
  $("artifact-size").textContent = "Checking…";
  $("artifact-detail").textContent = "Ready when the code checks pass.";
  $("check-state").textContent = compilerReady ? "Checking…" : "Waiting for Python…";
  diagnostics([], compilerReady ? "Checking your latest changes…" : "Python is loading. Your latest changes will be checked when it is ready.");
  try {
    localStorage.setItem(storageKey, JSON.stringify({ source: source(), filename: $("filename").textContent }));
    $("draft-state").textContent = "Draft saved in this browser";
  } catch { $("draft-state").textContent = "Draft available for this session"; }
  buttons(); debounce = setTimeout(check, 220);
}
function check() {
  if (!compilerReady) return;
  clearTimeout(compilationDeadline);
  compilationDeadline = setTimeout(() => compilerFailed("The compiler took too long. Retry the compiler or simplify the source."), 10000);
  worker.postMessage({ id: revision, source: source(), target });
}
function compilerFailed(message) {
  compilerReady = false; artifact = null; worker?.terminate();
  clearTimeout(initializationDeadline); clearTimeout(compilationDeadline);
  $("compiler-state").textContent = "Compiler unavailable"; $("compiler-badge").className = "badge error";
  $("check-state").textContent = "Compiler unavailable"; $("artifact-size").textContent = "Unavailable";
  diagnostics([], message); $("retry-compiler").hidden = false; log(message, "error"); buttons();
}
function startCompiler() {
  worker?.terminate(); compilerReady = false; artifact = null;
  $("retry-compiler").hidden = true; $("compiler-badge").className = "badge";
  $("compiler-state").textContent = "Loading compiler";
  worker = new Worker(new URL("./compiler-worker.mjs", import.meta.url), { type: "module" });
  initializationDeadline = setTimeout(() => compilerFailed("Compiler download timed out. Check your internet connection and retry."), 60000);
  worker.onerror = event => compilerFailed(event.message || "The compiler could not load. Check your internet connection and retry.");
  worker.onmessage = ({ data }) => {
    if (data.type === "status") { diagnostics([], data.message); return; }
    if (data.type === "fatal") { compilerFailed(data.message); return; }
    if (data.type === "ready") {
      clearTimeout(initializationDeadline); compilerReady = true;
      $("compiler-state").textContent = "Compiler ready"; $("compiler-badge").className = "badge ready";
      log("Python compiler ready. All compilation stays in this browser.", "success"); check(); return;
    }
    if (data.id !== revision) return; // Never show errors or upload bytes for old edits.
    clearTimeout(compilationDeadline);
    if (data.type === "failed") {
      artifact = null; $("check-state").textContent = "Compiler error"; $("artifact-size").textContent = "Unavailable";
      diagnostics([], data.message); log(data.message, "error"); buttons(); return;
    }
    artifact = data.diagnostics.length ? null : { ...data, source: source(), revision };
    $("check-state").textContent = artifact ? "All checks passed" : `${data.diagnostics.length} issue${data.diagnostics.length === 1 ? "" : "s"}`;
    $("artifact-size").textContent = artifact ? `${artifact.wasm.length.toLocaleString()} bytes` : "Code needs a fix";
    const parameters = artifact?.manifest.parameters?.length || 0;
    $("artifact-detail").textContent = artifact ? `ABI ${artifact.manifest.abi} · RGB8 · ${parameters} parameter${parameters === 1 ? "" : "s"}${target ? " · fits device" : ""}` : "Fix the highlighted issue before uploading.";
    diagnostics(data.diagnostics, "✓ Code checks passed. The compiled animation is ready."); buttons();
  };
  buttons();
}

function browserHelp() {
  if (!isSecureContext) notice("Open this page through HTTPS or http://localhost. Browser USB connections require a secure context.");
  else if (!navigator.serial) notice("USB Serial is unavailable in this browser. HTTP connections, editing and downloads remain available; browser local-network permissions may apply.");
  else notice();
  buttons();
}
async function action(operation) {
  const connection = client;
  busy = true; notice(); buttons();
  try { await operation(); }
  catch (error) {
    if (error.name !== "NotFoundError") {
      let refreshed = false;
      if (error.conflict && connection && !connection.closed) {
        try {
          await refreshDevice(connection); await refreshNetwork(connection);
          await refreshProcedures(connection); await refreshParameters(connection);
          refreshed = true;
        } catch {}
      }
      notice(error.message + (refreshed ? " Device state refreshed. Review it before trying again." : ""));
      log(error.message, "error");
    }
    else log("Device selection cancelled.");
  } finally { busy = false; buttons(); }
}
function disconnected(error) {
  parameterState = null;
  client = null; target = null; outputReady = false;
  $("device-summary").textContent = "Connect a LightVM board over USB.";
  $("device-dot").className = "status-dot"; $("device-details").hidden = true;
  if (error?.message !== "Connection closed.") { notice(error.message); log(error.message, "error"); }
  changed();
}

function pathUI() {
  const selector = $("command-path"); selector.replaceChildren();
  if (!paths.selected) { const option = document.createElement("option"); option.value = ""; option.textContent = "Select a connected path"; selector.append(option); }
  for (const [key, connection] of paths.paths) {
    const option = document.createElement("option"); option.value = key; option.textContent = connection.client.transport.label; selector.append(option);
  }
  selector.value = paths.selected || "";
  $("path-health").textContent = ["usb", "http"].map(key => `${key.toUpperCase()}: ${paths.paths.has(key) ? "connected" : "disconnected"}`).join(" · ");
  buttons();
}
async function refreshDevice(connection = client) {
  if (!connection) return;
  const description = await connection.describe(), config = await connection.json("config_get");
  if (connection !== client) return;
  target = description; const binding = config.targets?.A?.device, output = config.devices?.find(device => device.id === binding);
  outputReady = Boolean(output?.config?.count); $("config-json").value = JSON.stringify(config, null, 2);
  $("device-summary").textContent = connection.transport.label; $("device-dot").className = "status-dot connected"; $("device-details").hidden = false;
  $("board").textContent = description.board; $("module-limit").textContent = `${description.module_limit.toLocaleString()} bytes`;
  $("output-binding").textContent = outputReady ? `${output.config.count} pixels · slot A` : "Slot A is not configured";
  parameterState = null; $("parameters").replaceChildren();
  $("parameter-state").textContent = "Load a control program to edit its parameters.";
  buttons();
}
async function connectPath(key, transport) {
  let candidate;
  try {
    await transport.open();
    candidate = new LightVMClient(transport, { onOutput: text => log(text), onDisconnect: error => {
      if (paths.paths.get(key)?.client === candidate) paths.detach(key);
      if (client === candidate) disconnected(error);
      pathUI();
    } });
    const description = await candidate.describe();
    if (description.control_abi !== 2 || !description.program_schemas?.includes("lightvm.program/3")) throw new Error("Update this firmware to use Control ABI 2 programs.");
    paths.attach(key, candidate, description); client = candidate; target = description;
    await refreshDevice(candidate); await refreshNetwork(candidate); await refreshProcedures(candidate); await refreshParameters(candidate);
    log(`Connected to ${description.board} via ${key.toUpperCase()}.`, "success"); pathUI(); changed();
  } catch (error) { if (candidate) await candidate.close(); else await transport.close(); throw error; }
}
$("connect").onclick = () => action(() => connectPath("usb", new SerialTransport()));
$("connect-http").onclick = () => action(async () => {
  let address = $("http-address").value;
  if (new URL(address).hostname === "lightvm.local") {
    const result = await discovery.read(address);
    const device = target ? result.devices.find(device => device.device_id === target.device_id) : result.devices[0];
    if (!device) throw new Error("The connected USB device is absent from this device list. Refresh discovery or enter its address.");
    address = device.url; $("http-address").value = address;
  }
  await connectPath("http", new HttpTransport(address));
});
async function findDevices() {
  if (findingDevices) return;
  findingDevices = true; $("discovery-state").textContent = "Looking for devices…"; buttons();
  try {
    const seeds = [paths.paths.get("http")?.client.transport?.url, suggestedHttpAddress, $("http-address").value, discovery.seed];
    const result = await discovery.find(seeds);
    const selected = $("nearby-devices").value;
    const previousAddress = nearbyDevices.find(device => device.device_id === selected)?.url;
    nearbyDevices = result.devices;
    const options = [new Option("Choose a device", ""), ...nearbyDevices.map(device => new Option(`${device.board} · ${device.hostname}`, device.device_id))];
    const select = $("nearby-devices");
    // Keep selector nodes/focus stable when a background refresh is unchanged.
    if (select.options.length !== options.length || options.some((option, index) => option.value !== select.options[index]?.value || option.text !== select.options[index]?.text)) select.replaceChildren(...options);
    if (nearbyDevices.some(device => device.device_id === selected)) select.value = selected;
    const updated = nearbyDevices.find(device => device.device_id === selected);
    if (updated && $("http-address").value === previousAddress) $("http-address").value = updated.url;
    const matching = target ? nearbyDevices.find(device => device.device_id === target.device_id) : nearbyDevices[0];
    if (!$("http-address").value && matching) { $("http-address").value = matching.url; select.value = matching.device_id; }
    $("discovery-state").textContent = `${nearbyDevices.length} ${nearbyDevices.length === 1 ? "device" : "devices"} found${result.truncated ? " · list full; use a device address for others" : ""}.`;
  } catch (error) {
    nearbyDevices = []; $("nearby-devices").replaceChildren(new Option("No devices found", ""));
    $("discovery-state").textContent = error.message;
  } finally { findingDevices = false; buttons(); }
}
$("discover-devices").onclick = findDevices;
$("nearby-devices").onchange = () => {
  const device = nearbyDevices.find(device => device.device_id === $("nearby-devices").value);
  if (device) $("http-address").value = device.url;
};
// Discovery reads only metadata and never creates a session or changes the
// selected command path. Periodic refresh picks up new devices and expirations.
findDevices();
setInterval(() => { if (!busy && !document.hidden) findDevices(); }, 10000);
$("command-path").onchange = () => action(async () => {
  paths.select($("command-path").value); client = paths.active.client; target = paths.active.description;
  await refreshDevice(); await refreshNetwork(); await refreshProcedures(); await refreshParameters(); pathUI();
});
$("disconnect").onclick = () => action(async () => { const connection = client; await connection.close(); log("Path disconnected. Autonomous playback continues."); });
$("upload").onclick = () => action(async () => {
  const compiled = artifact, connection = client, limit = target.module_limit;
  if (!compiled || compiled.revision !== revision || !connection || !outputReady) throw new Error("Wait for the current code checks and a configured device before uploading.");
  $("upload-progress").hidden = false; $("upload-progress").value = 0;
  $("upload").textContent = "Uploading…";
  log(`Uploading ${compiled.wasm.length.toLocaleString()} bytes…`);
  try {
    await connection.upload(compiled.wasm, limit, progress => { $("upload-progress").value = progress; });
    const reply = await connection.command(COMMAND.PLAY);
    log(`Animation running on ${reply.value} pixels.`, "success"); await refreshParameters(connection);
    if (source() !== compiled.source) notice("The uploaded animation is running. Your newer editor changes have not been uploaded yet.");
  } finally { $("upload").textContent = "Upload & run →"; $("upload-progress").hidden = true; }
});
$("stop").onclick = () => action(async () => { await client.command(COMMAND.STOP); log("Playback stopped. Lights cleared.", "success"); });

function download(bytes, filename, type) {
  const url = URL.createObjectURL(new Blob([bytes], { type })), anchor = document.createElement("a");
  anchor.href = url; anchor.download = filename; anchor.click(); setTimeout(() => URL.revokeObjectURL(url), 1000);
}
$("download").onclick = () => { if (artifact) download(artifact.wasm, $("filename").textContent.replace(/\.py$/i, "") + ".wasm", "application/wasm"); };
$("save-source").onclick = () => download(source(), $("filename").textContent, "text/x-python");
$("open-source").onclick = () => $("source-file").click();
function replaceSource(text, filename) {
  $("filename").textContent = filename;
  if (editor) editor.setValue(text); else { textarea.value = text; changed(); }
}
$("source-file").onchange = async () => {
  const file = $("source-file").files[0]; if (!file) return;
  const expected = ++fileRevision;
  try {
    if (file.size > 65536) throw new Error("Choose a Python file smaller than 64 KiB.");
    const text = await file.text();
    if (fileRevision === expected) { replaceSource(text, file.name); $("example").value = ""; log(`Opened ${file.name}.`); }
  } catch (error) { notice(error.message); }
  $("source-file").value = "";
};
$("example").onchange = async () => {
  const name = $("example").value; if (!name) return;
  const expected = ++fileRevision;
  try {
    const response = await fetch(new URL(`./samples/${name}.py`, import.meta.url));
    if (!response.ok) throw new Error(`Could not load the ${name} example.`);
    const text = await response.text();
    if (fileRevision === expected) { replaceSource(text, `${name}.py`); log(`Loaded ${name}.py.`); }
  } catch (error) { notice(error.message); }
};
$("clear-log").onclick = () => $("activity").replaceChildren();
$("retry-compiler").onclick = startCompiler;
window.addEventListener("beforeunload", () => { worker?.terminate(); for (const { client } of paths.paths.values()) void client.close(); });

function wifiRow(network = {}) {
  const row = document.createElement("div"); row.className = "wifi-network";
  const ssidLabel = document.createElement("label"), ssid = document.createElement("input"); ssidLabel.textContent = "SSID"; ssid.value = network.ssid || ""; ssid.autocomplete = "off"; ssidLabel.append(ssid);
  const passwordLabel = document.createElement("label"), password = document.createElement("input"); passwordLabel.textContent = "Password"; password.type = "password"; password.placeholder = network.ssid ? "Keep saved password" : "Enter password"; passwordLabel.append(password);
  for (const input of [ssid, password]) {
    input.autocomplete = "off"; input.spellcheck = false;
    input.setAttribute("autocapitalize", "off"); input.setAttribute("autocorrect", "off");
    input.setAttribute("data-1p-ignore", "true"); input.setAttribute("data-lpignore", "true");
    input.setAttribute("data-bwignore", "true"); input.setAttribute("data-form-type", "other");
  }
  const hiddenLabel = document.createElement("label"), hidden = document.createElement("input"); hidden.type = "checkbox"; hidden.checked = Boolean(network.hidden); hiddenLabel.append(hidden, " Hidden SSID");
  const openLabel = document.createElement("label"), open = document.createElement("input"); open.type = "checkbox"; openLabel.append(open, " Use an open network"); open.onchange = () => { password.disabled = open.checked; if (open.checked) password.value = ""; };
  const controls = document.createElement("div"); controls.className = "button-pair";
  for (const [label, move] of [["Move up", -1], ["Move down", 1], ["Remove", 0]]) {
    const button = document.createElement("button"); button.className = "quiet"; button.textContent = label;
    button.onclick = () => { if (!move) row.remove(); else if (move < 0 && row.previousElementSibling) row.parentNode.insertBefore(row, row.previousElementSibling); else if (move > 0 && row.nextElementSibling) row.parentNode.insertBefore(row.nextElementSibling, row); };
    controls.append(button);
  }
  row.append(ssidLabel, passwordLabel, hiddenLabel, openLabel, controls);
  row.read = () => ({ ssid: ssid.value, hidden: hidden.checked, ...(open.checked ? { password: "" } : password.value ? { password: password.value } : {}) });
  $("wifi-networks").append(row);
}
async function refreshNetwork(connection = client) {
  if (!connection || !(target?.wifi_supported || target?.http_supported)) { $("network-state").textContent = "Networking is excluded from this firmware profile."; return; }
  const config = await connection.json("network_get"), state = await connection.json("network_status");
  if (connection !== client) return;
  $("wifi-enabled").checked = config.wifi.enabled; $("wifi-country").value = config.wifi.country;
  $("http-enabled").checked = config.http.enabled; $("http-port").value = config.http.port;
  $("wifi-networks").replaceChildren(); config.wifi.networks.forEach(wifiRow);
  updateNetworkStatus(state);
}
function updateNetworkStatus(state) {
  const text = `${state.state}${state.ssid ? ` · ${state.ssid}` : ""}${state.ip ? ` · ${state.ip}` : ""}${state.error ? ` · ${state.error}` : ""}${state.retry_ms ? ` · retry in ${Math.ceil(state.retry_ms / 1000)}s` : ""}`;
  if ($("network-state").textContent !== text) $("network-state").textContent = text;
  const address = $("http-address");
  if (state.ip && !paths.paths.has("http") && document.activeElement !== address && (!address.value || address.value === suggestedHttpAddress)) {
    const suggestion = `http://${state.ip}:${state.http_port}`;
    if (address.value !== suggestion) address.value = suggestion;
    suggestedHttpAddress = suggestion;
  }
}
async function refreshProcedures(connection = client) {
  if (!connection) return;
  const procedures = await connection.json("procedure_list"); if (connection !== client) return;
  for (const id of ["procedure-select", "startup-procedure"]) {
    const select = $(id), selected = select.value; select.replaceChildren();
    if (id === "startup-procedure") { const option = document.createElement("option"); option.value = ""; option.textContent = "Stay idle on reboot"; select.append(option); }
    for (const item of procedures) { const option = document.createElement("option"); option.value = item.name; option.textContent = `${item.name} · ${item.size} bytes`; select.append(option); }
    if (procedures.some(item => item.name === selected)) select.value = selected;
  }
  const config = await connection.json("config_get"); if (connection === client) $("startup-procedure").value = config.startup?.procedure || "";
}
async function refreshParameters(connection = client) {
  if (!connection) return;
  let state;
  try { state = await connection.json("program_parameters_get"); }
  catch (error) { if (connection.closed) throw error; if (connection === client) { parameterState = null; $("parameters").replaceChildren(); $("parameter-state").textContent = "Load a control program to edit its parameters."; } buttons(); return; }
  if (connection !== client) return;
  parameterState = state; $("parameters").replaceChildren();
  $("parameter-state").textContent = `${state.faulted ? "Faulted" : state.paused ? "Paused" : "Running"} · slot ${state.slot}`;
  for (const parameter of state.manifest.parameters) {
    const label = document.createElement("label"), input = document.createElement("input"); label.textContent = parameter.name;
    if (parameter.type === "bool") { input.type = "checkbox"; input.checked = state.values[parameter.name]; }
    else if (parameter.type === "rgb8") { input.type = "color"; input.value = `#${state.values[parameter.name].toString(16).padStart(6, "0")}`; }
    else { input.type = parameter.type === "int" ? "text" : "number"; if (parameter.type === "int") input.inputMode = "numeric"; input.step = "any"; input.value = state.values[parameter.name]; if (parameter.minimum !== undefined) input.min = parameter.minimum; if (parameter.maximum !== undefined) input.max = parameter.maximum; }
    input.dataset.name = parameter.name; input.dataset.type = parameter.type; label.append(input); $("parameters").append(label);
  }
  buttons();
}
$("wifi-add").onclick = () => { if ($("wifi-networks").children.length < 8) wifiRow(); };
$("network-refresh").onclick = () => action(() => refreshNetwork());
$("network-save").onclick = () => action(async () => {
  const connection = client;
  const config = { schema: "lightvm.network/1", wifi: { enabled: $("wifi-enabled").checked, country: $("wifi-country").value.toUpperCase(), networks: Array.from($("wifi-networks").children, row => row.read()) }, http: { enabled: $("http-enabled").checked, port: Number($("http-port").value) } };
  try { await connection.configureNetwork(config); log("Network settings saved. Reconnect HTTP after the network changes.", "success"); }
  finally { for (const input of $("wifi-networks").querySelectorAll("input[type=password]")) input.value = ""; }
});
$("config-refresh").onclick = () => action(() => refreshDevice());
for (const [id, options] of [["config-validate", { validate: true }], ["config-apply", {}], ["config-save", { save: true }]]) $(id).onclick = () => action(async () => { const connection = client; await connection.configure(JSON.parse($("config-json").value), options); if (!options.validate) await refreshDevice(connection); log(options.validate ? "Configuration is valid." : "Output configuration applied; playback stopped.", "success"); });
$("startup-save").onclick = () => action(async () => { const connection = client, config = await connection.json("config_get"), name = $("startup-procedure").value; config.startup = name ? { procedure: name, target: "A" } : null; await connection.configure(config, { save: true }); await refreshDevice(connection); log("Startup selection saved; playback stopped.", "success"); });
$("procedures-refresh").onclick = () => action(() => refreshProcedures());
$("procedure-save").onclick = () => action(async () => { const connection = client, compiled = artifact; if (!compiled || compiled.revision !== revision) throw new Error("Wait for current compilation."); const name = $("procedure-name").value; if (!/^[A-Za-z0-9_.-]{1,32}$/.test(name)) throw new Error("Use 1–32 letters, digits, underscores, dots or hyphens for the procedure name."); await connection.saveProcedure(name, compiled.wasm); await refreshProcedures(connection); log(`Saved ${name}.`, "success"); });
$("procedure-activate").onclick = () => action(async () => { const connection = client; await connection.manage("procedure_activate", { name: $("procedure-select").value, slot: "A" }); await refreshParameters(connection); log("Stored procedure activated.", "success"); });
$("procedure-download").onclick = () => action(async () => { const name = $("procedure-select").value, reply = await client.manage("procedure_get", { name }); download(reply.data, `${name}.wasm`, "application/wasm"); });
$("procedure-delete").onclick = () => action(async () => { const connection = client; await connection.deleteProcedure($("procedure-select").value); await refreshProcedures(connection); log("Procedure deleted.", "success"); });
$("parameters-refresh").onclick = () => action(() => refreshParameters());
$("parameters-apply").onclick = () => action(async () => { const connection = client, state = parameterState, values = Object.create(null); for (const input of $("parameters").querySelectorAll("input")) {
    if (input.dataset.type === "int" && !/^-?\d+$/.test(input.value)) throw new Error("Enter a whole signed integer.");
    if (input.dataset.type === "float" && (!input.value || !Number.isFinite(Number(input.value)))) throw new Error("Enter a finite number.");
    const value = input.dataset.type === "bool" ? input.checked : input.dataset.type === "rgb8" ? parseInt(input.value.slice(1), 16) : input.dataset.type === "int" ? BigInt(input.value) : Number(input.value);
    if (String(value) !== String(state.values[input.dataset.name])) values[input.dataset.name] = value;
  } try { await connection.setParameters(values, state); } finally { if (!connection.closed) await refreshParameters(connection); } });
for (const verb of ["pause", "resume"]) $(`program-${verb}`).onclick = () => action(async () => { const connection = client; await connection.manage(`program_${verb}`, { expected_generation: parameterState.generation }); await refreshParameters(connection); });
setInterval(async () => {
  if (busy || polling || document.hidden || !paths.paths.size) return;
  polling = true;
  try {
    for (const [key, { client: connection, description }] of paths.paths) {
      const current = await connection.describe();
      if (busy) break;
      if (paths.paths.get(key)?.client !== connection) continue;
      if (current.boot_id !== description.boot_id) { await connection.close(); continue; }
      if (connection === client && (target?.wifi_supported || target?.http_supported)) {
        const state = await connection.json("network_status");
        if (busy) break;
        if (connection === client && paths.paths.get(key)?.client === connection) updateNetworkStatus(state);
      }
    }
  } catch (error) { if (!busy) notice(error.message); } finally { polling = false; }
}, 5000);
browserHelp(); startCompiler(); changed();
