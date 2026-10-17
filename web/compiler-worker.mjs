import { loadPyodide } from "https://cdn.jsdelivr.net/pyodide/v314.0.7/full/pyodide.mjs";

let pyodide;
const status = message => self.postMessage({ type: "status", message });
const ready = (async () => {
  status("Loading Python…");
  pyodide = await loadPyodide();
  status("Loading the LightVM compiler…");
  pyodide.FS.mkdirTree("/editor/lightvm");
  await Promise.all(["__init__.py", "compiler.py", "compiler_core.py", "portable.py", "sprite_compiler.py", "easing.py", "motion.py"].map(async name => {
    const response = await fetch(new URL(`./python/lightvm/${name}`, import.meta.url));
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
    # CPython AST columns count UTF-8 bytes. Convert them to character offsets
    # before the browser converts those to its UTF-16 editor coordinates.
    lines = source.splitlines()
    for item in diagnostics:
        if 0 < item["line"] <= len(lines):
            text = lines[item["line"] - 1]
            item["column"] = len(text.encode()[:max(0, item["column"] - 1)].decode(errors="ignore")) + 1
    return json.dumps({"diagnostics": diagnostics, "manifest": result.manifest,
        "bytecode": list(result.bytecode)}, allow_nan=False)
`);
  self.postMessage({ type: "ready" });
})().catch(error => {
  self.postMessage({ type: "fatal", message: error.message || String(error) });
  throw error;
});
// Avoid an unhandled initialization rejection if no compile request is pending.
void ready.catch(() => {});

let queue = Promise.resolve();
self.onmessage = ({ data }) => {
  queue = queue.then(async () => {
    try {
      await ready;
      if (data.source.length > 65536) throw new Error("Keep source below 64 KiB for this prototype.");
      pyodide.globals.set("editor_source", data.source);
      pyodide.globals.set("editor_target", JSON.stringify(data.target ?? null));
      const result = JSON.parse(pyodide.runPython("editor_compile(editor_source, editor_target)"));
      result.bytecode = new Uint8Array(result.bytecode || []);
      self.postMessage({ type: "compiled", id: data.id, ...result }, [result.bytecode.buffer]);
    } catch (error) {
      self.postMessage({ type: "failed", id: data.id, message: error.message || String(error) });
    }
  });
};
