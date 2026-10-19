import { EXTENSIONS, LANGUAGE_LABELS } from './languages/source';
import { useEffect, useRef, useState, type ReactNode } from 'react';
import { Link } from 'react-router-dom';
import { SettingFields } from './SettingFields';
import { InputRecipeFields } from './InputRecipeFields';
import { supportsProgram, programCompatibility } from './compiler';
import { download } from './controller-service';
import { useRuntime } from './runtime';
import { useAppSelector } from './store';
import type { InputBindings } from './types';
import { inputsFor, settingsFor, type SettingsValues } from './program';
import { parseProgramFile, sourceName, type ProgramFile } from './program-file';

export function DevelopmentWorkflow({ editorPanel }: { editorPanel: ReactNode }) {
  const { compiler, controllers } = useRuntime();
  const editor = useAppSelector(state => state.editor), controller = useAppSelector(state => state.controller);
  const activity = useAppSelector(state => state.activity);
  const [settings, setSettings] = useState<SettingsValues>({}), [inputs, setInputs] = useState<InputBindings>({});
  const [validInputs, setValidInputs] = useState(true), [name, setName] = useState(() => sourceName(editor.filename));
  const [saved, setSaved] = useState(''), [previewed, setPreviewed] = useState(''), [deployed, setDeployed] = useState('');
  const [stored, setStored] = useState('');
  const picker = useRef<HTMLInputElement>(null), filename = useRef(editor.filename);
  const recipes = useRef(new Map<string, { name: string; settings: SettingsValues; inputs: InputBindings; validInputs: boolean; saved: string }>());
  const savedSources = useRef(new Map<string, string>());
  const draft: ProgramFile = { schema: 'lightvm.source', filename: editor.filename, name, source: editor.source, settings, inputs };
  const signature = JSON.stringify(draft);
  const busy = Boolean(activity.operation), artifact = editor.artifact;
  const output = controller.config?.outputs.find(item => item.id === controller.selectedOutput && item.mode !== 'indicators');
  const transition = artifact?.manifest.profile === 'transition';
  const compatible = Boolean(artifact && (!controller.description || supportsProgram(controller.description, artifact.manifest)));
  const connected = Boolean(controller.selectedPath), writable = controller.description?.storage_writable !== false;
  const deployable = !busy && connected && writable && compatible && artifact && artifact.bytes <= (controller.description?.module_limit ?? 0);
  const validName = /^[A-Za-z0-9_.-]{1,32}$/.test(name) && !name.startsWith('_preview');
  const selected = controller.programs.find(item => (item.type ?? item.profile) + ':' + item.name === stored);
  useEffect(() => {
    if (filename.current === editor.filename) return;
    recipes.current.set(filename.current, { name, settings, inputs, validInputs, saved });
    filename.current = editor.filename;
    const previous = recipes.current.get(editor.filename);
    setName(previous?.name ?? sourceName(editor.filename)); setSettings(previous?.settings ?? {});
    setInputs(previous?.inputs ?? {}); setValidInputs(previous?.validInputs ?? true); setSaved(previous?.saved ?? '');
  }, [editor.filename]);
  useEffect(() => { setDeployed(''); setPreviewed(''); setStored(''); }, [controller.description?.controller_id, controller.description?.boot_id]);
  useEffect(() => {
    const unload = (event: BeforeUnloadEvent) => { if (signature !== saved || Object.entries(editor.drafts).some(([language, draft]) => language !== editor.language && savedSources.current.get(draft.filename) !== draft.source)) { event.preventDefault(); event.returnValue = ''; } };
    window.addEventListener('beforeunload', unload);
    return () => window.removeEventListener('beforeunload', unload);
  }, [signature, saved, editor.drafts, editor.language]);
  async function open(file?: File) {
    if (!file) return;
    const revision = controllers.snapshot.editor.revision;
    await controllers.run('Importing source file', async () => {
      if (file.size > 256 * 1024) throw new Error('Choose a source file smaller than 256 KiB.');
      const document = parseProgramFile(await file.text(), file.name);
      if (revision !== controllers.snapshot.editor.revision) throw new Error('The draft changed while importing. Import the file again.');
      filename.current = document.filename;
      compiler.edit(document.source, document.filename); setName(document.name); setSettings(document.settings); setInputs(document.inputs); setValidInputs(true);
      savedSources.current.set(document.filename, document.source);
      setSaved(JSON.stringify(document)); setPreviewed(''); setDeployed('');
      controllers.log('Imported ' + file.name + '.', 'success');
    });
    if (picker.current) picker.current.value = '';
  }
  function current() {
    const compiled = compiler.artifact;
    if (!compiled || compiled.id !== controllers.snapshot.editor.revision) throw new Error('Wait for the current code checks to finish.');
    if (!validInputs) throw new Error('Fix the input recipe first.');
    return { compiled, overrides: settingsFor(compiled.manifest, settings), wiring: inputsFor(compiled.manifest, inputs) };
  }
  async function preview() {
    if (!transition) {
      try {
        const { compiled, overrides, wiring } = current();
        if (await controllers.upload(compiled.bytecode, compiled.source, compiled.id, overrides, wiring)) setPreviewed(signature);
      } catch (error) { await controllers.run('Previewing animation', async () => { throw error; }); }
      return;
    }
    await controllers.run('Previewing transition', async client => {
      const { compiled, overrides, wiring } = current(), connection = controllers.require(client);
      const state = await connection.transition(output!.id);
      if (!state.A.active || !state.B.active) throw new Error('Load both A and B in Mixer before previewing a transition.');
      // Firmware selects transition programs by stored name. Keep a single scratch
      // entry separate from the user's named deployment, never a browser library.
      await connection.saveProgram('_preview_transition', compiled.bytecode);
      await controllers.readPrograms(connection);
      await connection.selectTransition('_preview_transition', output!.id, { settings: overrides, ...(Object.keys(wiring).length ? { inputs: wiring } : {}) });
      await connection.runTransition(state.progress < 50 ? 100 : 0, 2000, output!.id);
      setPreviewed(signature); controllers.log('Transition preview running between A and B.', 'success');
    });
  }
  async function deploy() {
    await controllers.run('Deploying program', async client => {
      const { compiled } = current();
      if (!validName) throw new Error('Use 1–32 letters, digits, dots, underscores or hyphens. The _preview prefix is reserved.');
      const connection = controllers.require(client);
      await connection.saveProgram(name, compiled.bytecode); await controllers.readPrograms(connection);
      setDeployed(signature); controllers.log('Deployed ' + name + '. Use it in Configuration presets or Mixer.', 'success');
    });
  }
  async function saveFile() {
    await controllers.run('Saving source file', async () => {
      if (!validInputs) throw new Error('Fix the input recipe before saving the file.');
      if (Object.values(settings).some(value => typeof value !== 'boolean' && !Number.isFinite(value))) throw new Error('Enter a value for each enabled setting before saving the file.');
      download(JSON.stringify(draft, null, 2), name + '.lightvm.json', 'application/json'); savedSources.current.set(editor.filename, editor.source); setSaved(signature);
    });
  }
  return <>
    <div className="file-actions"><div><span className="badge">{transition ? 'Transition' : artifact ? 'Animation' : LANGUAGE_LABELS[editor.language] + ' program'}</span><span className="helper" role="status">{signature === saved ? 'File up to date' : 'Unsaved file changes'}</span></div>
      <button id="open-source" className="secondary" disabled={busy} onClick={() => picker.current?.click()}>Import file</button>
      <input ref={picker} id="source-file" type="file" hidden accept=".py,.js,.json" aria-label="Import source file" onChange={event => void open(event.target.files?.[0])} />
    </div>
    <div className="workspace editor-workspace">{editorPanel}<aside className="sidebar">
      <section className="card"><h2>Program</h2><label htmlFor="program-name">Program name</label><input id="program-name" value={name} maxLength={32} onChange={event => setName(event.target.value)} />
        <label htmlFor="preview-output">Preview output</label><select id="preview-output" value={output?.id ?? ''} disabled={busy || !connected} onChange={event => void controllers.selectOutput(event.target.value)}>
          {!output && <option value="">Connect and configure an output</option>}{controller.config?.outputs.filter(item => item.mode !== 'indicators').map(item => <option key={item.id} value={item.id}>{item.id} · {item.config.count} pixels</option>)}</select>
        <p className="helper">{transition ? 'Load A and B in Mixer first. Preview stages a scratch transition on the device and runs a two-second fade.' : 'Preview runs on this output and replaces its current playback.'}</p>
        <button id="upload" className="secondary full" disabled={!deployable || !output || !validInputs} onClick={() => void preview()}>Preview on device <span aria-hidden="true">▶</span></button>
        {previewed === signature && <p className="helper" role="status">Preview sent to {output?.id}.</p>}
        <button id="save-source" className="secondary full" disabled={busy || !validName} onClick={() => void saveFile()}>Save locally <span aria-hidden="true">↓</span></button>
        <p className="helper">Downloads source and settings as .lightvm.json. Import it to continue editing.</p>
        <button id="deploy" className="primary full" disabled={!deployable || !validName || !validInputs} onClick={() => void deploy()}>Deploy to device <span aria-hidden="true">↗</span></button>
        {deployed === signature && <p className="check-ok" role="status">{name} deployed.</p>}
        <p className="helper">Deploy stores the compiled program. Set layer settings and input bindings in Configuration → Presets.</p>
        {activity.progress !== null && <progress max={1} value={activity.progress} aria-label="Program upload progress" />}
        {artifact && !compatible && <p role="alert" className="helper">{controller.description && programCompatibility(controller.description, artifact.manifest)}</p>}
        {!connected && <Link className="quiet-link" to="/controllers">Connect a device to preview and deploy →</Link>}
      </section>
      {artifact && <details className="card"><summary>Preview settings &amp; inputs</summary><fieldset disabled={busy}><SettingFields manifest={artifact.manifest} values={settings} onChange={setSettings} prefix="draft" />
        <InputRecipeFields key={editor.filename} manifest={artifact.manifest} values={inputs} onChange={setInputs} onValidity={setValidInputs} prefix="draft" /></fieldset></details>}
      <details className="card"><summary>Code &amp; exports</summary><p id="artifact-size" className="helper">{artifact ? artifact.bytes.toLocaleString() + ' bytes · ' + artifact.manifest.profile : editor.message}</p>
        <div className="button-pair"><button className="quiet" onClick={() => download(editor.source, editor.filename, editor.language === 'blocks' ? 'application/json' : 'text/plain')}>Export {EXTENSIONS[editor.language]}</button><button id="download" className="quiet" disabled={!artifact} onClick={() => { if (compiler.artifact) download(new Uint8Array(compiler.artifact.bytecode), name + '.lbc', 'application/octet-stream'); }}>Download .lbc</button></div>
        {editor.compiler === 'error' && <button onClick={() => compiler.retry()}>Retry compiler</button>}
      </details>
    </aside></div>
    {connected && <details className="card device-programs"><summary>Programs on device · {controller.programs.length}</summary><p className="helper">Compiled animations and transitions. Keep the external source file for editing.</p>
      <label htmlFor="stored-program">Stored compiled program</label><select id="stored-program" value={stored} onChange={event => setStored(event.target.value)}><option value="">Choose a program</option>{controller.programs.map(item => <option key={(item.type ?? item.profile) + ':' + item.name} value={(item.type ?? item.profile) + ':' + item.name}>{item.name} ({item.type ?? item.profile})</option>)}</select>
      <div className="button-pair"><button disabled={busy || !selected} onClick={() => void controllers.run('Downloading program', async client => {
        const bytes = await controllers.require(client).getProgram(selected!.name, selected!.type ?? selected!.profile);
        download(new Uint8Array(bytes), selected!.name + '.lbc', 'application/octet-stream');
      })}>Download program</button><button className="quiet" disabled={busy || !selected || !writable} onClick={() => void controllers.run('Deleting program', async client => {
        const connection = controllers.require(client); await connection.deleteProgram(selected!.name, selected!.type ?? selected!.profile); await controllers.readPrograms(connection); setStored('');
      })}>Delete program</button></div></details>}
  </>;
}
