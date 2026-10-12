import { LightVMClient, COMMAND } from "./protocol.mjs";
import { SerialTransport } from "./transports.mjs";

const $ = id => document.getElementById(id);
const textarea = $("source"), storageKey = "lightvm.editor.draft.v1";
let editor, worker, compilerReady = false, revision = 0, artifact = null;
let client = null, target = null, outputReady = false, busy = false;
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
  $("connect").disabled = busy || Boolean(client) || !isSecureContext || !navigator.serial;
  $("connect").hidden = Boolean(client); $("disconnect").hidden = !client;
  $("disconnect").disabled = busy;
  $("upload").disabled = busy || !client || !artifact || !outputReady;
  $("stop").disabled = busy || !client;
  $("download").disabled = !artifact;
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
  else if (!navigator.serial) notice("USB Serial is unavailable in this browser. Open the page in desktop Chrome or Edge. Editing and .wasm downloads remain available.");
  else notice();
  buttons();
}
async function action(operation) {
  busy = true; notice(); buttons();
  try { await operation(); }
  catch (error) {
    if (error.name !== "NotFoundError") { notice(error.message); log(error.message, "error"); }
    else log("Device selection cancelled.");
  } finally { busy = false; buttons(); }
}
function disconnected(error) {
  client = null; target = null; outputReady = false;
  $("device-summary").textContent = "Connect a LightVM board over USB.";
  $("device-dot").className = "status-dot"; $("device-details").hidden = true;
  if (error?.message !== "Connection closed.") { notice(error.message); log(error.message, "error"); }
  changed();
}

$("connect").onclick = () => action(async () => {
  // Keep the chooser in the click's user activation; do not await compiler work.
  const transport = new SerialTransport();
  let candidate;
  try {
    await transport.open();
    candidate = new LightVMClient(transport, { onOutput: text => log(text), onDisconnect: error => { if (client === candidate) disconnected(error); } });
    await candidate.status();
    const description = await candidate.describe();
    if (description.control_abi !== 2 || !description.program_schemas?.includes("lightvm.program/3")) {
      throw new Error("This firmware does not support the editor's Control ABI 2 programs. Update it before uploading.");
    }
    const config = JSON.parse(new TextDecoder().decode((await candidate.manage("config_get")).data));
    const binding = config.targets?.A?.device, output = config.devices?.find(device => device.id === binding);
    target = description; outputReady = Boolean(output?.config?.count);
    client = candidate;
    $("device-summary").textContent = transport.label;
    $("device-dot").className = "status-dot connected"; $("device-details").hidden = false;
    $("board").textContent = description.board;
    $("module-limit").textContent = `${description.module_limit.toLocaleString()} bytes`;
    $("output-binding").textContent = outputReady ? `${output.config.count} pixels · slot A` : "Slot A is not configured";
    if (!outputReady) notice("The device is connected, but slot A has no configured light strip. Configure its pin and pixel count with the existing LightVM configuration tools before uploading.");
    log(`Connected to ${description.board} via USB Serial.`, "success"); changed();
  } catch (error) { if (candidate) await candidate.close(); else await transport.close(); throw error; }
});
$("disconnect").onclick = () => action(async () => { await client.close(); log("Device disconnected. Autonomous playback continues."); });
$("upload").onclick = () => action(async () => {
  const compiled = artifact, connection = client, limit = target.module_limit;
  if (!compiled || compiled.revision !== revision || !connection || !outputReady) throw new Error("Wait for the current code checks and a configured device before uploading.");
  $("upload-progress").hidden = false; $("upload-progress").value = 0;
  $("upload").textContent = "Uploading…";
  log(`Uploading ${compiled.wasm.length.toLocaleString()} bytes…`);
  try {
    await connection.upload(compiled.wasm, limit, progress => { $("upload-progress").value = progress; });
    const reply = await connection.command(COMMAND.PLAY);
    log(`Animation running on ${reply.value} pixels.`, "success");
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
window.addEventListener("beforeunload", () => { worker?.terminate(); void client?.close(); });
browserHelp(); startCompiler(); changed();
