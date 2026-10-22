import { lowerSource } from './languages/lower';
import { sourceDiagnostic, type LoweredSource } from './languages/source';
import { COMPILER_FILES } from './compiler-files.mjs';

const pyodideUrl = "https://cdn.jsdelivr.net/pyodide/v314.0.7/full/pyodide.mjs";
let initialize: (base: string) => void;
const initialized = new Promise<string>(resolve => { initialize = resolve; });

let pyodide: any;
const status = (message: string) => self.postMessage({ type: "status", message });
const ready = (async () => {
  const baseUrl = await initialized;
  const { loadPyodide } = await import(/* @vite-ignore */ pyodideUrl);
  status("Loading the shared compiler…");
  pyodide = await loadPyodide();
  status("Loading the LightVM compiler…");
  pyodide.FS.mkdirTree("/editor/lightvm");
  await Promise.all(COMPILER_FILES.map(async name => {
    const response = await fetch(new URL(`./python/lightvm/${name}`, baseUrl));
    if (!response.ok) throw new Error(`Could not load ${name} (${response.status}). Rebuild the static editor assets.`);
    pyodide.FS.writeFile(`/editor/lightvm/${name}`, await response.text());
  }));
  // Only this trusted wrapper is executed. Editor source is passed as data to
  // ast/symtable and the restricted compiler; it is never run by Python.
  pyodide.runPython(`
import sys, ast, symtable, json
from dataclasses import asdict
sys.path.insert(0, "/editor")
from lightvm.compiler import compile_source

def editor_compile(source, target_json):
    try:
        ast.parse(source, filename="animation.py")
        symtable.symtable(source, "animation.py", "exec")
    except SyntaxError as error:
        return json.dumps({"diagnostics": [{"message": error.msg,
            "line": error.lineno or 1, "column": error.offset or 1,
            "end_line": error.end_lineno or error.lineno or 1,
            "end_column": error.end_offset or error.offset or 1,
            "syntax": True}]})
    result = compile_source(source, "animation.py", json.loads(target_json))
    diagnostics = [asdict(item) for item in result.diagnostics]
    advisories = [asdict(item) for item in result.advisories]
    # CPython AST columns count UTF-8 bytes. Convert them to character offsets
    # before the browser converts those to its UTF-16 editor coordinates.
    lines = source.splitlines()
    for item in diagnostics + advisories:
        if 0 < item["line"] <= len(lines):
            text = lines[item["line"] - 1]
            item["column"] = len(text.encode()[:max(0, item["column"] - 1)].decode(errors="ignore")) + 1
    return json.dumps({"diagnostics": diagnostics, "advisories": advisories, "manifest": result.manifest,
        "bytecode": list(result.bytecode)}, allow_nan=False)
`);
  self.postMessage({ type: "ready" });
})().catch(error => {
  self.postMessage({ type: "fatal", message: error instanceof Error ? error.message : String(error) });
  throw error;
});
// Avoid an unhandled initialization rejection if no compile request is pending.
void ready.catch(() => {});

let queue = Promise.resolve();
self.onmessage = ({ data }) => {
  if (data.type === 'initialize') { initialize(data.baseUrl); return; }
  queue = queue.then(async () => {
    try {
      await ready;
      let lowered: LoweredSource;
      try { lowered = lowerSource(data.source, data.language ?? 'python'); }
      catch (error) { self.postMessage({ type: 'compiled', id: data.id, diagnostics: [sourceDiagnostic(error)], bytecode: new Uint8Array(), manifest: {} }); return; }
      pyodide.globals.set("editor_source", lowered.python);
      pyodide.globals.set("editor_target", JSON.stringify(data.target ?? null));
      const result = JSON.parse(pyodide.runPython("editor_compile(editor_source, editor_target)"));
      if (data.language !== 'python') for (const key of ['diagnostics', 'advisories']) {
        result[key] = (result[key] ?? []).map((item: any) => ({
          ...item, ...lowered.locations[item.line - 1], filename: data.filename,
          message: item.message.replaceAll('@init', 'init').replaceAll('@render', 'render'),
        }));
      }
      result.bytecode = new Uint8Array(result.bytecode || []);
      self.postMessage({ type: "compiled", id: data.id, ...result });
    } catch (error) {
      self.postMessage({ type: "failed", id: data.id, message: error instanceof Error ? error.message : String(error) });
    }
  });
};
