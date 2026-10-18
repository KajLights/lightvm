import { useEffect, useRef, useState } from 'react';
import { useAppSelector } from './store';
import { useRuntime } from './runtime';
import { EXAMPLES, exampleForSource } from './examples';
import { download } from './device-service';
import { DevelopmentWorkflow } from './Workflow';
import type { Diagnostic } from './types';

function Editor() {
  const { compiler, devices } = useRuntime(), editor = useAppSelector(state => state.editor);
  const textarea = useRef<HTMLTextAreaElement>(null), cm = useRef<any>(null), marks = useRef<any[]>([]);
  const [cursor, setCursor] = useState('Ln 1, Col 1');
  const example = exampleForSource(editor.source);
  const picker = useRef<HTMLInputElement>(null);
  useEffect(() => {
    compiler.start();
    if (!window.CodeMirror || !textarea.current) return;
    const instance = window.CodeMirror.fromTextArea(textarea.current, {
      mode: { name: 'python', version: 3 }, lineNumbers: true, indentUnit: 4, tabSize: 4,
      indentWithTabs: false, autoCloseBrackets: true, matchBrackets: true,
      gutters: ['CodeMirror-linenumbers', 'error-gutter'],
      extraKeys: {
        Tab: (editor: any) => editor.somethingSelected() ? editor.indentSelection('add') : editor.replaceSelection('    ', 'end'),
        'Shift-Tab': (editor: any) => editor.indentSelection('subtract'),
        'Ctrl-Enter': () => document.getElementById('upload')?.click(),
        'Cmd-Enter': () => document.getElementById('upload')?.click(),
      },
    });
    cm.current = instance; instance.getInputField().setAttribute('aria-label', 'Animation Python source');
    instance.on('change', () => compiler.edit(instance.getValue()));
    instance.on('cursorActivity', () => { const pos = instance.getCursor(); setCursor('Ln ' + (pos.line + 1) + ', Col ' + (pos.ch + 1)); });
    return () => { marks.current.forEach(mark => mark.clear()); marks.current = []; cm.current = null; instance.toTextArea(); };
  }, [compiler]);
  useEffect(() => { if (cm.current && cm.current.getValue() !== editor.source) cm.current.setValue(editor.source); }, [editor.source]);
  useEffect(() => {
    const instance = cm.current;
    if (!instance) return;
    marks.current.forEach(mark => mark.clear()); marks.current = []; instance.clearGutter('error-gutter');
    for (const item of editor.diagnostics) {
      const line = Math.max(0, Math.min(item.line - 1, instance.lineCount() - 1));
      const content = instance.getLine(line), column = Math.max(0, Math.min(item.column - 1, content.length - 1));
      if (content.length) marks.current.push(instance.markText({ line, ch: column }, { line, ch: column + 1 }, { className: 'code-error', title: item.message }));
      const marker = document.createElement('span'); marker.className = 'error-marker'; marker.textContent = '●'; marker.title = item.message;
      instance.setGutterMarker(line, 'error-gutter', marker);
    }
  }, [editor.diagnostics]);
  async function open(file?: File) {
    if (!file) return;
    try {
      if (file.size > 65536) throw new Error('Choose a Python file smaller than 64 KiB.');
      const revision = devices.snapshot.editor.revision;
      const source = await file.text();
      if (revision === devices.snapshot.editor.revision) compiler.edit(source, file.name);
    } catch (error) { void devices.run('Opening source', async () => { throw error; }); }
    if (picker.current) picker.current.value = '';
  }
  function loadExample(name: string) {
    const selected = EXAMPLES.find(item => item.id === name);
    if (selected) compiler.edit(selected.source, selected.filename);
  }
  function focusDiagnostic(item: Diagnostic) {
    if (cm.current) { cm.current.focus(); cm.current.setCursor({ line: item.line - 1, ch: item.column - 1 }); cm.current.scrollIntoView({ line: item.line - 1, ch: item.column - 1 }, 60); }
    else textarea.current?.focus();
  }
  return <section className="editor-panel" aria-label="Python source editor">
    <div className="editor-toolbar"><div className="file-label"><span className="python-dot" /><span id="filename">{editor.filename}</span></div><div className="toolbar-actions"><label className="sr-only" htmlFor="example">Load example</label><select id="example" value={example?.id ?? ''} onChange={event => loadExample(event.target.value)}><option value="">Examples…</option>{EXAMPLES.map(item => <option key={item.id} value={item.id}>{item.label}</option>)}</select><button id="open-source" className="quiet" onClick={() => picker.current?.click()}>Open .py</button><button id="save-source" className="quiet" onClick={() => download(editor.source, editor.filename, 'text/x-python')}>Export .py</button></div></div>
    {example && <p id="example-description" className="helper">{example.description}</p>}
    <label className="sr-only" htmlFor="source">Animation Python source</label><textarea ref={textarea} id="source" value={editor.source} spellCheck={false} autoCapitalize="off" autoComplete="off" onChange={event => compiler.edit(event.target.value)} />
    <input ref={picker} id="source-file" type="file" accept=".py,text/x-python,text/plain" hidden onChange={event => void open(event.target.files?.[0])} />
    <div className="editor-footer"><span id="cursor-position">{cursor}</span><span>Python subset · Portable bytecode</span><span id="draft-state">{editor.draftSaved ? 'Draft saved in this browser' : 'Draft available for this session'}</span></div>
    <div className="diagnostics-header"><h2>Code checks</h2><span id="check-state">{editor.compiler === 'error' ? 'Compiler unavailable' : editor.checking ? 'Checking…' : editor.diagnostics.length ? 'Code needs attention' : editor.artifact ? 'All checks passed' : 'Waiting for Python…'}</span></div>
    <div id="diagnostics" className="diagnostics" role="status" aria-live="polite">{editor.diagnostics.length ? editor.diagnostics.map((item, index) => <button key={index} className="diagnostic" onClick={() => focusDiagnostic(item)}><strong>Line {item.line}:{item.column}</strong><span>{item.message}</span></button>) : <p className={editor.artifact ? 'check-ok' : 'muted'}>{editor.compiler === 'error' ? editor.message : editor.artifact ? '✓ Code checks passed. The compiled animation is ready.' : editor.message}</p>}</div>
  </section>;
}

function Playback() {
  const { devices } = useRuntime();
  const { programState: state, selectedPath } = useAppSelector(state => state.device);
  const busy = useAppSelector(state => state.activity.operation !== null);
  async function playback(verb: 'pause' | 'resume') {
    if (!state) return;
    await devices.run(verb === 'pause' ? 'Pausing program' : 'Resuming program', async client => {
      const connection = devices.require(client);
      await connection.manage('program_' + verb, { device: state.device, layer: state.layer, expected_generation: state.generation });
      await devices.readProgramState(connection);
    });
  }
  return <section className="card"><div className="card-heading"><h2>Playback</h2><button className="quiet small" disabled={!selectedPath || busy} onClick={() => void devices.run('Refreshing playback', async client => devices.readProgramState(devices.require(client)))}>Refresh</button></div>
    <p id="playback-state" className="helper">{state ? (state.faulted ? 'Faulted' : state.paused ? 'Paused' : 'Running') : 'Load an animation to control playback.'}</p>
    <fieldset disabled={!state || !selectedPath || busy}><div className="button-pair"><button id="program-pause" className="quiet" disabled={!state || state.paused} onClick={() => void playback('pause')}>Pause</button><button id="program-resume" className="quiet" disabled={!state || !state.paused} onClick={() => void playback('resume')}>Resume</button></div></fieldset>
  </section>;
}
export function Develop({ active }: { active: boolean }) {
  const description = useAppSelector(state => state.device.description), connected = useAppSelector(state => Boolean(state.device.selectedPath));
  const { compiler } = useRuntime();
  useEffect(() => { if (active) compiler.start(); }, [active, compiler]);
  return <><div className="intro"><div><p className="eyebrow">DEVELOP</p><h1>Make something glow.</h1><p>Edit, preview on your lights, save an animation, then compose and save a preset.</p></div><div className="pipeline" aria-label="Development workflow"><span>Edit</span><span>→</span><span>Preview</span><span>→</span><span>Save</span><span>→</span><span>Compose</span><span>→</span><span>Preset</span></div></div>
    {description && !connected && <p className="connection-banner">Connect the device to upload. The editor and downloads remain available.</p>}
    <DevelopmentWorkflow editorPanel={<Editor />} playback={<Playback />} active={active} />
  </>;
}
