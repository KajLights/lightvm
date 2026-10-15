import { useEffect, useRef, useState } from 'react';
import { Link } from 'react-router-dom';
import { activityActions, outputFor, useAppDispatch, useAppSelector } from './store';
import { useRuntime } from './runtime';
import { assetUrl } from './compiler';
import { download } from './device-service';
import type { Diagnostic, ParameterState } from './types';

function Editor() {
  const { compiler, devices } = useRuntime(), editor = useAppSelector(state => state.editor);
  const textarea = useRef<HTMLTextAreaElement>(null), cm = useRef<any>(null), marks = useRef<any[]>([]);
  const [cursor, setCursor] = useState('Ln 1, Col 1'), [example, setExample] = useState('');
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
      if (revision === devices.snapshot.editor.revision) { compiler.edit(source, file.name); setExample(''); }
    } catch (error) { void devices.run('Opening source', async () => { throw error; }); }
    if (picker.current) picker.current.value = '';
  }
  async function loadExample(name: string) {
    setExample(name); if (!name) return;
    const revision = devices.snapshot.editor.revision;
    try {
      const response = await fetch(assetUrl('samples/' + name + '.py'));
      if (!response.ok) throw new Error('Could not load the ' + name + ' example.');
      const source = await response.text();
      if (revision === devices.snapshot.editor.revision) compiler.edit(source, name + '.py');
    } catch (error) { void devices.run('Loading example', async () => { throw error; }); }
  }
  function focusDiagnostic(item: Diagnostic) {
    if (cm.current) { cm.current.focus(); cm.current.setCursor({ line: item.line - 1, ch: item.column - 1 }); cm.current.scrollIntoView({ line: item.line - 1, ch: item.column - 1 }, 60); }
    else textarea.current?.focus();
  }
  return <section className="editor-panel" aria-label="Python source editor">
    <div className="editor-toolbar"><div className="file-label"><span className="python-dot" /><span id="filename">{editor.filename}</span></div><div className="toolbar-actions"><label className="sr-only" htmlFor="example">Load example</label><select id="example" value={example} onChange={event => void loadExample(event.target.value)}><option value="">Examples…</option>{['rainbow', 'twinkle', 'color', 'pattern', 'wipe', 'chase'].map(name => <option key={name}>{name}</option>)}</select><button id="open-source" className="quiet" onClick={() => picker.current?.click()}>Open .py</button><button id="save-source" className="quiet" onClick={() => download(editor.source, editor.filename, 'text/x-python')}>Save .py</button></div></div>
    <label className="sr-only" htmlFor="source">Animation Python source</label><textarea ref={textarea} id="source" value={editor.source} spellCheck={false} autoCapitalize="off" autoComplete="off" onChange={event => compiler.edit(event.target.value)} />
    <input ref={picker} id="source-file" type="file" accept=".py,text/x-python,text/plain" hidden onChange={event => void open(event.target.files?.[0])} />
    <div className="editor-footer"><span id="cursor-position">{cursor}</span><span>Python subset · Control ABI 2</span><span id="draft-state">{editor.draftSaved ? 'Draft saved in this browser' : 'Draft available for this session'}</span></div>
    <div className="diagnostics-header"><h2>Code checks</h2><span id="check-state">{editor.compiler === 'error' ? 'Compiler unavailable' : editor.checking ? 'Checking…' : editor.artifact ? 'All checks passed' : 'Waiting for Python…'}</span></div>
    <div id="diagnostics" className="diagnostics" role="status" aria-live="polite">{editor.diagnostics.length ? editor.diagnostics.map((item, index) => <button key={index} className="diagnostic" onClick={() => focusDiagnostic(item)}><strong>Line {item.line}:{item.column}</strong><span>{item.message}</span></button>) : <p className={editor.artifact ? 'check-ok' : 'muted'}>{editor.compiler === 'error' ? editor.message : editor.artifact ? '✓ Code checks passed. The compiled animation is ready.' : editor.message}</p>}</div>
  </section>;
}

function SendProgram() {
  const { compiler, devices } = useRuntime(), editor = useAppSelector(state => state.editor);
  const device = useAppSelector(state => state.device), activity = useAppSelector(state => state.activity), dispatch = useAppDispatch();
  const artifact = editor.artifact, canUpload = Boolean(artifact && device.selectedPath && outputFor(device.config) && artifact.bytes <= (device.description?.module_limit || 0));
  const incompatible = Boolean(device.description && (device.description.control_abi !== 2 || !device.description.program_schemas?.includes('lightvm.program/3')));
  async function upload() {
    const compiled = compiler.artifact;
    if (!compiled || compiled.id !== devices.snapshot.editor.revision) return;
    await devices.upload(compiled.bytecode, compiled.source, compiled.id);
  }
  return <section className="card"><div className="card-heading"><h2>Send your animation</h2></div><p className="muted">Compilation happens in your browser. Only the bytecode program is sent to the device.</p>
    <div className="artifact"><span>Compiled program</span><strong id="artifact-size">{artifact ? artifact.bytes.toLocaleString() + ' bytes' : 'Checking…'}</strong></div><p id="artifact-detail" className="helper">{artifact ? 'ABI ' + artifact.manifest.abi + ' · RGB8 · ' + artifact.manifest.parameters.length + ' parameters' : 'Ready when the code checks pass.'}</p>
    {incompatible && <p className="helper">This device needs current Control ABI 2 firmware for Python programs.</p>}
    {!outputFor(device.config) && device.selectedPath && <p className="helper">Configure output on slot A before uploading. <Link to={'/devices/' + device.description?.device_id + '/configure'}>Open configuration</Link></p>}
    <button id="upload" className="primary full" disabled={Boolean(activity.operation) || !canUpload || incompatible} onClick={() => void upload()}>{activity.operation === 'Uploading animation' ? 'Uploading…' : 'Upload & run →'}</button>
    {activity.progress !== null && <progress id="upload-progress" max={1} value={activity.progress} aria-label="Program upload progress" />}
    <div className="button-pair"><button id="download" className="secondary" disabled={!artifact} onClick={() => { if (compiler.artifact) download(new Uint8Array(compiler.artifact.bytecode), editor.filename.replace(/\.py$/i, '') + '.lbc', 'application/octet-stream'); }}>Download .lbc</button><button id="stop" className="quiet" disabled={Boolean(activity.operation) || !device.selectedPath} onClick={() => void devices.stopPlayback()}>Stop lights</button></div>
    <p className="helper deployment-note">Upload replaces the running program. Saving a program to device storage is a separate step.</p>
    {editor.compiler === 'error' && <button id="retry-compiler" className="quiet" onClick={() => compiler.retry()}>Retry compiler</button>}
  </section>;
}

function Procedures() {
  const { compiler, devices } = useRuntime(), device = useAppSelector(state => state.device), activity = useAppSelector(state => state.activity), editor = useAppSelector(state => state.editor);
  const [name, setName] = useState(''), [selected, setSelected] = useState('');
  const available = Boolean(device.selectedPath && device.description?.storage_available), busy = Boolean(activity.operation);
  useEffect(() => { if (!device.procedures.some(item => item.name === selected)) setSelected(device.procedures[0]?.name || ''); }, [device.procedures, selected]);
  async function save() {
    await devices.run('Saving program', async client => {
      const compiled = compiler.artifact;
      if (!compiled || compiled.id !== devices.snapshot.editor.revision) throw new Error('Wait for the latest compilation.');
      if (!/^[A-Za-z0-9_.-]{1,32}$/.test(name)) throw new Error('Use 1–32 letters, digits, underscores, dots or hyphens for the procedure name.');
      const connection = devices.require(client);
      await connection.saveProcedure(name, compiled.bytecode); await devices.readProcedures(connection); devices.log('Saved ' + name + '.', 'success');
    });
  }
  async function activate() {
    await devices.run('Activating program', async client => {
      const connection = devices.require(client); await connection.manage('procedure_activate', { name: selected, slot: 'A' });
      await devices.readParameters(connection); devices.log('Stored program activated.', 'success');
    });
  }
  async function remove() {
    await devices.run('Deleting program', async client => {
      const connection = devices.require(client); await connection.deleteProcedure(selected);
      await devices.readProcedures(connection); devices.log('Program deleted.', 'success');
    });
  }
  return <section className="card"><div className="card-heading"><h2>Stored programs</h2><button id="procedures-refresh" className="quiet small" disabled={!available || busy} onClick={() => void devices.run('Refreshing programs', async client => devices.readProcedures(devices.require(client)))}>Refresh</button></div>
    <p className="muted">Save your compiled animation to device storage, or run one already saved there.</p>
    <fieldset id="procedure-fields" disabled={!available || busy}>
      <label htmlFor="procedure-name">Save compiled animation as</label><input id="procedure-name" value={name} onChange={event => setName(event.target.value)} placeholder="rainbow" maxLength={32} autoComplete="off" />
      <button id="procedure-save" className="secondary full" disabled={!editor.artifact} onClick={() => void save()}>Save compiled bytecode</button>
      <label htmlFor="procedure-select">Stored program</label><select id="procedure-select" value={selected} onChange={event => setSelected(event.target.value)}>{!device.procedures.length && <option value="">No programs saved</option>}{device.procedures.map(item => <option key={item.name} value={item.name}>{item.name + ' · ' + item.size + ' bytes'}</option>)}</select>
      <div className="button-pair"><button id="procedure-activate" className="secondary" disabled={!selected || !outputFor(device.config)} onClick={() => void activate()}>Activate on A</button><button id="procedure-download" className="quiet" disabled={!selected} onClick={() => void devices.run('Downloading program', async client => { const reply = await devices.require(client).manage('procedure_get', { name: selected }); download(new Uint8Array(reply.data), selected + '.lbc', 'application/octet-stream'); })}>Download</button></div>
      <button id="procedure-delete" className="quiet full" disabled={!selected} onClick={() => void remove()}>Delete selected program</button>
    </fieldset>
    <p className="helper">Saving adds to the library. Startup behaviour is set in Configure.</p>
  </section>;
}

function Parameters() {
  const { devices } = useRuntime(), state = useAppSelector(store => store.device.parameters), selectedPath = useAppSelector(store => store.device.selectedPath), busy = useAppSelector(store => Boolean(store.activity.operation));
  const [values, setValues] = useState<Record<string, string | boolean>>({});
  useEffect(() => {
    if (!state) { setValues({}); return; }
    setValues(Object.fromEntries(state.manifest.parameters.map(parameter => [parameter.name, parameter.type === 'rgb8' ? '#' + Number(state.values[parameter.name]).toString(16).padStart(6, '0') : parameter.type === 'bool' ? Boolean(state.values[parameter.name]) : String(state.values[parameter.name])])));
  }, [state]);
  async function apply() {
    if (!state) return;
    const patch: Record<string, bigint | number | boolean> = {};
    for (const parameter of state.manifest.parameters) {
      const input = values[parameter.name];
      if (parameter.type === 'int') {
        if (!/^-?\d+$/.test(String(input))) { await devices.run('Applying parameters', async () => { throw new Error('Enter a whole signed integer.'); }); return; }
        if (String(input) !== String(state.values[parameter.name])) patch[parameter.name] = BigInt(String(input));
      } else if (parameter.type === 'float') {
        if (!String(input) || !Number.isFinite(Number(input))) { await devices.run('Applying parameters', async () => { throw new Error('Enter a finite number.'); }); return; }
        if (Number(input) !== Number(state.values[parameter.name])) patch[parameter.name] = Number(input);
      } else if (parameter.type === 'rgb8') {
        const color = parseInt(String(input).slice(1), 16);
        if (color !== Number(state.values[parameter.name])) patch[parameter.name] = color;
      } else if (input !== state.values[parameter.name]) patch[parameter.name] = Boolean(input);
    }
    await devices.setParameters(patch, state);
  }
  async function playback(verb: 'pause' | 'resume') {
    if (!state) return;
    await devices.run(verb === 'pause' ? 'Pausing program' : 'Resuming program', async client => {
      const connection = devices.require(client);
      await connection.manage('program_' + verb, { expected_generation: state.generation });
      await devices.readParameters(connection);
    });
  }
  return <section className="card"><div className="card-heading"><h2>Live parameters</h2><button id="parameters-refresh" className="quiet small" disabled={!selectedPath || busy} onClick={() => void devices.run('Refreshing parameters', async client => devices.readParameters(devices.require(client)))}>Refresh</button></div>
    <p id="parameter-state" className="helper">{state ? (state.faulted ? 'Faulted' : state.paused ? 'Paused' : 'Running') + ' · slot ' + state.slot : 'Load a control program to edit its parameters.'}</p>
    <fieldset id="parameter-fields" disabled={!state || !selectedPath || busy}><div id="parameters">{state?.manifest.parameters.map(parameter => <label key={parameter.name}>{parameter.name}<input data-name={parameter.name} data-type={parameter.type} type={parameter.type === 'bool' ? 'checkbox' : parameter.type === 'rgb8' ? 'color' : parameter.type === 'int' ? 'text' : 'number'} inputMode={parameter.type === 'int' ? 'numeric' : undefined} step={parameter.type === 'float' ? 'any' : undefined} min={parameter.minimum} max={parameter.maximum} checked={parameter.type === 'bool' ? Boolean(values[parameter.name]) : undefined} value={parameter.type !== 'bool' ? String(values[parameter.name] ?? '') : undefined} onChange={event => setValues(current => ({ ...current, [parameter.name]: parameter.type === 'bool' ? event.target.checked : event.target.value }))} /></label>)}</div>
      <button id="parameters-apply" className="secondary full" onClick={() => void apply()}>Apply parameter values</button><div className="button-pair"><button id="program-pause" className="quiet" disabled={!state || state.paused} onClick={() => void playback('pause')}>Pause</button><button id="program-resume" className="quiet" disabled={!state || !state.paused} onClick={() => void playback('resume')}>Resume</button></div>
    </fieldset>
  </section>;
}
export function Develop({ active }: { active: boolean }) {
  const description = useAppSelector(state => state.device.description), connected = useAppSelector(state => Boolean(state.device.selectedPath));
  const { compiler } = useRuntime();
  useEffect(() => { if (active) compiler.start(); }, [active, compiler]);
  return <><div className="intro"><div><p className="eyebrow">DEVELOP</p><h1>Make something glow.</h1><p>Write an animation, check it as you type, and send it to your lights.</p></div><div className="pipeline" aria-label="Compilation flow"><span>Python</span><span aria-hidden="true">→</span><span>WebAssembly</span><span aria-hidden="true">→</span><span>Device</span></div></div>
    {description && !connected && <p className="connection-banner">Connect the device to upload. The editor and downloads remain available.</p>}
    <div className="workspace"><Editor /><aside className="sidebar"><SendProgram /><Procedures /><Parameters /></aside></div>
  </>;
}
