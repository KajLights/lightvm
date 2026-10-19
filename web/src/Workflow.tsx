import { PresetTransition } from './PresetTransition';
import { RecallTransition } from './RecallTransition';
import { SettingFields } from './SettingFields';
import { InputRecipeFields } from './InputRecipeFields';
import { LiveInputs } from './LiveInputs';
import { crc32 } from '../protocol.mjs';
import { TransitionControls } from './Transitions';
import { VERSION } from './release';
import { useEffect, useRef, useState, type ReactNode } from 'react';
import { Link } from 'react-router-dom';
import type { LightVMClient } from '../protocol.mjs';
import { supportsProgram, programCompatibility } from './compiler';
import { download } from './controller-service';
import { useRuntime } from './runtime';
import { useAppSelector } from './store';
import type { InputBindings, Manifest, TransitionSpec } from './types';
import { prepareLibraryComposition } from './library-controller';
import { BLENDS, programName, validTransitionSpec, compositionLayers, importLibrary, inputsFor, readLibrary, saveAnimation, settingsFor, writeLibrary,
  type Blend, type Library, type PresetLayer, type SavedAnimation, type SettingsValues } from './library';


export function DevelopmentWorkflow({ editorPanel, playback, active = true }: { editorPanel: ReactNode; playback: ReactNode; active?: boolean }) {
  const { compiler, controllers } = useRuntime();
  const editor = useAppSelector(state => state.editor), controller = useAppSelector(state => state.controller), activity = useAppSelector(state => state.activity);
  const [initial] = useState(readLibrary), [library, setLibrary] = useState(initial.library);
  const [storageError, setStorageError] = useState(initial.error);
  const [settings, setSettings] = useState<SettingsValues>({}), [animationName, setAnimationName] = useState('');
  const [inputs, setInputs] = useState<InputBindings>({}), [validInputs, setValidInputs] = useState(true);
  const [invalidLayers, setInvalidLayers] = useState<Record<string, boolean>>({});
  const [selectedAnimation, setSelectedAnimation] = useState(''), [layers, setLayers] = useState<PresetLayer[]>([]);
  const [presetName, setPresetName] = useState(''), [selectedPreset, setSelectedPreset] = useState(''), [storedPreset, setStoredPreset] = useState('');
  const [storedProgram, setStoredProgram] = useState('');
  const [entryTransition, setEntryTransition] = useState<TransitionSpec>();
  const storedSelection = controller.programs.find(item => (item.type ?? item.profile ?? 'animation') + ':' + item.name === storedProgram);
  const [compositionSlot, setCompositionSlot] = useState('A');
  const [transitionName, setTransitionName] = useState('crossfade');
  const picker = useRef<HTMLInputElement>(null);
  const busy = Boolean(activity.operation), output = controller.config?.outputs.find(item => item.id === controller.selectedOutput);
  const connected = Boolean(controller.selectedPath && output), artifact = editor.artifact;
  const writable = controller.description?.storage_writable !== false;
  const isTransition = artifact?.manifest.profile === 'transition';
  const compatible = Boolean(artifact && (!controller.description || supportsProgram(controller.description, artifact.manifest)));
  const selected = library.animations.find(item => item.id === selectedAnimation);
  // Example/file changes get their own defaults. Ordinary source edits retain overrides.
  const filename = useRef(editor.filename);
  useEffect(() => { if (filename.current !== editor.filename) { filename.current = editor.filename; setSettings({}); setInputs({}); setValidInputs(true); } }, [editor.filename]);
  useEffect(() => { setStoredPreset(''); setStoredProgram(''); }, [controller.description?.controller_id]);
  useEffect(() => { setTransitionName(editor.filename.replace(/\.py$/i, '').replace(/[^A-Za-z0-9_.-]/g, '_').slice(0, 32)); }, [editor.filename]);

  function commit(next: Library) {
    if (storageError) throw new Error(storageError);
    writeLibrary(next); setLibrary(next);
  }
  function single(animation: SavedAnimation) {
    if (animation.manifest.profile === 'transition') return;
    setSelectedAnimation(animation.id);
    setLayers([{ id: 'layer1', animation: animation.id, settings: { ...animation.settings }, ...(animation.inputs ? { inputs: structuredClone(animation.inputs) } : {}), opacity: 255, blend: 'over' }]);
  }
  async function preview() {
    const compiled = compiler.artifact;
    if (!compiled || compiled.id !== controllers.snapshot.editor.revision) return;
    let overrides: SettingsValues;
    let wiring: InputBindings;
    try { if (!validInputs) throw new Error('Fix the input recipe before previewing.'); overrides = settingsFor(compiled.manifest, settings); wiring = inputsFor(compiled.manifest, inputs); }
    catch (error) { await controllers.run('Previewing animation', async () => { throw error; }); return; }
    const revision = editor.revision, outputId = output?.id;
    if (compiled.manifest.profile === 'transition') {
      await controllers.run('Saving and selecting transition', async client => {
        if (!/^[A-Za-z0-9_.-]{1,32}$/.test(transitionName)) throw new Error('Give the transition a name of 1–32 letters, digits, dots, underscores or hyphens.');
        const connection = controllers.require(client);
        await connection.saveProgram(transitionName, compiled.bytecode);
        await controllers.readPrograms(connection);
        await connection.selectTransition(transitionName, outputId, { settings: overrides, ...(Object.keys(wiring).length ? { inputs: wiring } : {}) });
        controllers.log('Saved ' + transitionName + ' to flash and selected it between A and B.', 'success');
      });
      return;
    }
    if (await controllers.upload(compiled.bytecode, compiled.source, compiled.id, overrides, wiring) && controllers.snapshot.editor.revision === revision && controllers.snapshot.controller.selectedOutput === outputId) {
      const resolved = controllers.snapshot.controller.programState;
      if (resolved && !resolved.layer) setSettings(resolved.settings_values ?? overrides);
    }
  }
  async function save() {
    await controllers.run('Saving editable animation', async () => {
      const compiled = compiler.artifact;
      if (!compiled || compiled.id !== controllers.snapshot.editor.revision) throw new Error('Wait for the latest compilation.');
      if (!validInputs) throw new Error('Fix the input recipe before saving.');
      const saved = await saveAnimation(library, compiled, editor.filename, animationName, settings, inputs);
      commit(saved.library); setSelectedAnimation(saved.animation.id);
      if (layers.length <= 1) single(saved.animation);
      controllers.log('Saved ' + saved.animation.name + ' v' + saved.animation.version + ' in this browser.', 'success');
    });
  }
  async function openAnimation() {
    if (!selected) return;
    compiler.edit(selected.source, selected.filename); filename.current = selected.filename;
    setSettings({ ...selected.settings }); setAnimationName(selected.name);
    setInputs(structuredClone(selected.inputs ?? {})); setValidInputs(true);
  }
  function addLayer() {
    if (!selected || selected.manifest.profile === 'transition' || layers.length >= 8) return;
    let n = 1; while (layers.some(layer => layer.id === 'layer' + n)) n++;
    setLayers([...layers, { id: 'layer' + n, animation: selected.id, settings: { ...selected.settings }, opacity: 255, blend: 'over' }]);
  }
  function patchLayer(index: number, patch: Partial<PresetLayer>) { setLayers(layers.map((layer, n) => n === index ? { ...layer, ...patch } : layer)); }
  function moveLayer(index: number, delta: number) { const next = [...layers]; [next[index], next[index + delta]] = [next[index + delta], next[index]]; setLayers(next); }
  async function ensurePrograms(client: LightVMClient) {
    return prepareLibraryComposition(client, library, layers);
  }
  async function previewLayers() {
    await controllers.run('Previewing preset', async client => {
      const connection = controllers.require(client), outputId = controller.selectedOutput;
      if (layers.some(layer => invalidLayers[layer.id])) throw new Error('Fix the layer input recipes before previewing.');
      if (!outputId) throw new Error('Select an output first.');
      const recipe = await ensurePrograms(connection);
      await connection.configureComposition(recipe, outputId, compositionSlot); await controllers.readPrograms(connection); await controllers.readProgramState(connection);
      controllers.log('Preset preview running on ' + outputId + '.', 'success');
    });
  }
  function checkedPreset() {
    if (layers.some(layer => invalidLayers[layer.id])) throw new Error('Fix the layer input recipes before saving.');
    if (!/^[A-Za-z0-9_.-]{1,32}$/.test(presetName)) throw new Error('Use 1–32 letters, digits, dots, underscores or hyphens for the preset name.');
    compositionLayers(library, layers);
    if (entryTransition && !validTransitionSpec(entryTransition)) throw new Error('Choose a valid preset transition and duration.');
    return { ...(entryTransition ? { entry_transition: structuredClone(entryTransition) } : {}), id: library.presets.find(item => item.name === presetName)?.id ?? crypto.randomUUID(), name: presetName, layers: structuredClone(layers) };
  }
  async function savePreset() {
    await controllers.run('Saving preset', async () => {
      const preset = checkedPreset();
      commit({ ...library, presets: [...library.presets.filter(item => item.id !== preset.id), preset] });
      setSelectedPreset(preset.id); controllers.log('Preset saved in this browser. Export the library for a backup.', 'success');
    });
  }
  async function saveToController() {
    await controllers.run('Saving preset to controller', async client => {
      checkedPreset(); const connection = controllers.require(client), outputId = controller.selectedOutput;
      if (!outputId) throw new Error('Select an output first.');
      const recipe = await ensurePrograms(connection);
      await connection.savePreset(presetName, recipe, outputId, entryTransition);
      await controllers.readPrograms(connection); await controllers.readPresets(connection);
      setStoredPreset(presetName); controllers.log('Preset saved to controller flash. Startup is configured separately.', 'success');
    });
  }
  async function importFile(file?: File) {
    if (!file) return;
    await controllers.run('Importing animation library', async () => {
      if (file.size > 5 * 1024 * 1024) throw new Error('Choose a library smaller than 5 MiB.');
      const next = await importLibrary(library, await file.text());
      // A valid explicit import can recover an unreadable browser library.
      writeLibrary(next); setLibrary(next); setStorageError(''); controllers.log('Library imported.', 'success');
    });
    if (picker.current) picker.current.value = '';
  }
  const layerBytes = output ? (8 * layers.length + 9) * output.config.count : null;
  return <>
    <div className="workspace">{editorPanel}<aside className="sidebar">
      <section className="card"><div className="card-heading"><h2>Preview on lights</h2></div>
        <label htmlFor="preview-output">Preview output</label><select id="preview-output" value={controller.selectedOutput ?? ''} disabled={busy || !controller.selectedPath} onChange={event => void controllers.selectOutput(event.target.value)}>
          {!output && <option value="">Configure an output</option>}{controller.config?.outputs.filter(item => item.mode !== 'indicators').map(item => <option key={item.id} value={item.id}>{item.id + ' · ' + (item.config.pin ?? item.config.data_pin) + ' · ' + item.config.count + ' pixels'}</option>)}
        </select>
        {!output && controller.selectedPath && <p className="helper"><Link to={'/controllers/' + controller.description?.controller_id + '/configure'}>Configure an output</Link> before previewing.</p>}
        <p className="helper">{isTransition ? 'Save this transition to flash and select it between the prepared A and B compositions below.' : 'Preview replaces playback on this output. Other outputs continue. Saved presets and startup settings stay as saved.'}</p>
        <div className="artifact"><span>Compiled animation</span><strong id="artifact-size">{artifact ? artifact.bytes.toLocaleString() + ' bytes' : 'Checking…'}</strong></div>
        {artifact && <p id="artifact-detail" className="helper">LightVM {artifact.manifest.requires}+ · {(artifact.manifest.settings ?? []).length} settings</p>}
        <fieldset disabled={busy}><SettingFields manifest={artifact?.manifest ?? { compiler_version: VERSION, requires: '1.0.0', schema: 'lightvm.program', profile: 'animation', pixels: { format: 'rgba8-premul' }, settings: [] }} values={settings} onChange={setSettings} prefix="draft" /></fieldset>
        {artifact && <InputRecipeFields key={editor.filename} manifest={artifact.manifest} values={inputs} onChange={setInputs} onValidity={setValidInputs} prefix="draft" />}
        {isTransition && <><label htmlFor="transition-program-name">Stored transition name</label><input id="transition-program-name" value={transitionName} maxLength={32} onChange={event => setTransitionName(event.target.value)} /></>}
        <button id="upload" className="primary full" disabled={busy || !writable || !connected || !compatible || !artifact || artifact.bytes > (controller.description?.module_limit ?? 0)} onClick={() => void preview()}>{isTransition ? 'Save and select transition →' : 'Preview animation →'}</button>
        {activity.progress !== null && <progress id="upload-progress" max={1} value={activity.progress} aria-label="Program upload progress" />}
        <div className="button-pair"><button id="download" className="quiet" disabled={!artifact} onClick={() => { if (compiler.artifact) download(new Uint8Array(compiler.artifact.bytecode), editor.filename.replace(/\.py$/i, '') + '.lbc', 'application/octet-stream'); }}>Download .lbc</button><button id="stop" className="quiet" disabled={busy || !connected} onClick={() => void controllers.stopPlayback()}>Stop output</button></div>
        {artifact && !compatible && <p className="helper">{controller.description && artifact && programCompatibility(controller.description, artifact.manifest)}</p>}
        {editor.compiler === 'error' && <button id="retry-compiler" className="quiet" onClick={() => compiler.retry()}>Retry compiler</button>}
      </section>
      <section className="card"><div className="card-heading"><h2>{isTransition ? 'Save transition' : 'Save animation'}</h2></div>
        <p className="helper">Save editable source, compiled code and selected defaults in this browser. Each save creates a version; existing presets keep theirs.</p>
        <label htmlFor="animation-name">{isTransition ? 'Transition name' : 'Animation name'}</label><input id="animation-name" value={animationName} maxLength={80} onChange={event => setAnimationName(event.target.value)} />
        <button className="secondary full" disabled={busy || !artifact || !animationName.trim() || Boolean(storageError)} onClick={() => void save()}>{isTransition ? 'Save transition' : 'Save animation'}</button>
      </section>{playback}<LiveInputs active={active} onCapture={(bindings, running) => {
        const compiled = compiler.artifact;
        if (!compiled || compiled.id !== controllers.snapshot.editor.revision || running.program_crc32 !== crc32(compiled.bytecode)) {
          void controllers.run('Copying input recipe', async () => { throw new Error('Preview the current source before copying its live inputs into this draft.'); }); return;
        }
        setInputs(bindings); setValidInputs(true); controllers.log('Live input recipe copied into the draft. Save an animation version to keep it.', 'success');
      }} />
    </aside></div>
    <div className="workflow-grid">
      <section className="card"><div className="card-heading"><h2>Animation library</h2></div>
        <label htmlFor="saved-animation">Saved animation version</label><select id="saved-animation" value={selectedAnimation} onChange={event => setSelectedAnimation(event.target.value)}><option value="">Choose an animation</option>{library.animations.map(item => <option key={item.id} value={item.id}>{item.name + ' · v' + item.version}</option>)}</select>
        {selected && controller.description && programCompatibility(controller.description, selected.manifest) && <p role="status" className="helper">{programCompatibility(controller.description, selected.manifest)} You can still edit and export this animation.</p>}
        <div className="button-pair"><button className="secondary" disabled={busy || !selected} onClick={() => void openAnimation()}>{selected?.manifest.profile === 'transition' ? 'Edit transition' : 'Edit animation'}</button><button className="quiet" disabled={busy || !selected || selected.manifest.profile === 'transition'} onClick={() => { if (selected) single(selected); }}>Use alone</button></div>
        <button className="secondary full" disabled={busy || !selected || selected.manifest.profile === 'transition' || layers.length >= 8} onClick={addLayer}>Add as layer</button>
        <button className="quiet full" disabled={busy || !selected || Boolean(storageError)} onClick={() => void controllers.run('Deleting animation version', async () => {
          if (library.presets.some(preset => preset.layers.some(layer => layer.animation === selectedAnimation) || preset.entry_transition?.program === programName(selected!))) throw new Error('A saved browser preset uses this version. Delete or update that preset first.');
          commit({ ...library, animations: library.animations.filter(item => item.id !== selectedAnimation) });
          setLayers(layers.filter(layer => layer.animation !== selectedAnimation)); setSelectedAnimation('');
        })}>Delete animation version</button>
        <hr /><h3>Library backup</h3><p className="helper">Source stays in this browser. Export a library to move it to another browser or computer.</p>
        {storageError && <p role="alert">{storageError}</p>}
        <div className="button-pair"><button className="quiet" disabled={Boolean(storageError) && !initial.backup} onClick={() => download(storageError && initial.backup ? initial.backup : JSON.stringify(library, null, 2), 'lightvm-library.json', 'application/json')}>{storageError ? 'Export preserved library' : 'Export library'}</button><button className="quiet" disabled={busy} onClick={() => picker.current?.click()}>Import library</button></div>
        <input ref={picker} hidden type="file" accept=".json,application/json" aria-label="Import library file" onChange={event => void importFile(event.target.files?.[0])} />
      </section>
      <section className="card composer"><div className="card-heading"><h2>Compose &amp; save preset</h2></div>
        <p className="helper">Use one animation or combine several. Layers are listed from bottom to top; each has its own settings.</p>
        {!layers.length && <p className="muted">Save an animation, or choose one from the library.</p>}
        <fieldset disabled={busy}>{layers.map((layer, index) => {
          const animation = library.animations.find(item => item.id === layer.animation)!;
          return <div className="layer-card" key={layer.id}>
            <div className="card-heading"><h3>{index + 1}. {animation.name} <small>v{animation.version}</small></h3><div className="layer-actions"><button className="quiet small" aria-label={'Move layer ' + (index + 1) + ' down'} disabled={index === 0} onClick={() => moveLayer(index, -1)}>↓</button><button className="quiet small" aria-label={'Move layer ' + (index + 1) + ' up'} disabled={index === layers.length - 1} onClick={() => moveLayer(index, 1)}>↑</button><button className="quiet small" aria-label={'Remove layer ' + (index + 1)} onClick={() => setLayers(layers.filter((_, n) => n !== index))}>×</button></div></div>
            <div className="layer-controls"><label>Blend<select value={layer.blend} onChange={event => patchLayer(index, { blend: event.target.value as Blend })}>{BLENDS.map(blend => <option key={blend}>{blend}</option>)}</select></label><label>Opacity<input type="range" min={0} max={255} value={layer.opacity} onChange={event => patchLayer(index, { opacity: Number(event.target.value) })} /><span>{Math.round(layer.opacity / 255 * 100)}%</span></label></div>
            <SettingFields manifest={animation.manifest} values={layer.settings} onChange={values => patchLayer(index, { settings: values })} prefix={layer.id} />
            <InputRecipeFields manifest={animation.manifest} values={layer.inputs ?? animation.inputs ?? {}} onChange={values => patchLayer(index, { inputs: values })} onValidity={valid => setInvalidLayers(previous => {
              if (Boolean(previous[layer.id]) === !valid) return previous;
              return { ...previous, [layer.id]: !valid };
            })} prefix={layer.id} />
          </div>;
        })}</fieldset>
        {layerBytes !== null && layers.length > 0 && <p className="helper">Frame buffers: {layerBytes.toLocaleString()} bytes, plus animation state. The controller checks available memory before replacing playback. Composition also stores each shared program in its library.</p>}
        {controller.description?.ab_transitions && <><label htmlFor="preset-composition-slot">Preview composition slot</label><select id="preset-composition-slot" value={compositionSlot} onChange={event => setCompositionSlot(event.target.value)}><option>A</option><option>B</option></select></>}
        <button className="primary full" disabled={busy || !connected || !layers.length || !controller.description?.compositions} onClick={() => void previewLayers()}>Preview preset →</button>
        <label htmlFor="preset-name">Preset name</label><input id="preset-name" value={presetName} maxLength={32} onChange={event => setPresetName(event.target.value)} placeholder="blue-scanner" />
        <RecallTransition id="preset-entry-transition" label="Preset transition" value={entryTransition} onChange={setEntryTransition} disabled={busy} />
        <div className="button-pair"><button className="secondary" disabled={busy || !layers.length || !presetName || Boolean(storageError)} onClick={() => void savePreset()}>Save preset</button><button className="secondary" disabled={busy || !writable || !connected || !layers.length || !presetName || !controller.description?.presets} onClick={() => void saveToController()}>Save preset to controller</button></div>
        <p className="helper">Save preset keeps an editable recipe in this browser. Save to controller stores its compiled programs and recipe in flash. Saving an existing name replaces that preset. Choose a saved preset or restore the last state under Configure → Output & startup.</p>
        {connected && !controller.description?.presets && <p className="helper">Install firmware with preset storage support to save recipes on this controller.</p>}
      </section>
      <section className="card"><div className="card-heading"><h2>Presets</h2></div>
        <label htmlFor="saved-preset">Browser preset</label><select id="saved-preset" value={selectedPreset} onChange={event => setSelectedPreset(event.target.value)}><option value="">Choose a preset</option>{library.presets.map(item => <option key={item.id} value={item.id}>{item.name}</option>)}</select>
        <button className="secondary full" disabled={busy || !selectedPreset} onClick={() => { const preset = library.presets.find(item => item.id === selectedPreset); if (preset) { setLayers(structuredClone(preset.layers)); setPresetName(preset.name); setEntryTransition(preset.entry_transition ? structuredClone(preset.entry_transition) : undefined); } }}>Open preset</button>
        <button className="quiet full" disabled={busy || !selectedPreset || Boolean(storageError)} onClick={() => void controllers.run('Deleting browser preset', async () => {
          commit({ ...library, presets: library.presets.filter(item => item.id !== selectedPreset) }); setSelectedPreset('');
        })}>Delete browser preset</button>
        <hr /><label htmlFor="controller-preset">Controller preset</label><select id="controller-preset" value={storedPreset} onChange={event => setStoredPreset(event.target.value)}><option value="">Choose a controller preset</option>{controller.presets.map(item => <option key={item.name}>{item.name}</option>)}</select>
        {storedPreset && connected && output && <PresetTransition key={[controller.description?.controller_id, controller.description?.boot_id, storedPreset].join(':')} name={storedPreset} output={output.id} busy={busy || !writable} />}
        <div className="button-pair"><button className="secondary" disabled={busy || !connected || !storedPreset} onClick={() => void controllers.run('Running controller preset', async client => {
          const connection = controllers.require(client); await connection.recallPreset(storedPreset, controller.selectedOutput); await controllers.readProgramState(connection); controllers.log('Preset running on ' + controller.selectedOutput + '.', 'success');
        })}>Run on output</button><button className="quiet" disabled={busy || !writable || !controller.selectedPath || !storedPreset} onClick={() => void controllers.run('Deleting controller preset', async client => {
          const connection = controllers.require(client); await connection.deletePreset(storedPreset); await controllers.readPresets(connection); setStoredPreset('');
        })}>Delete controller preset</button></div>
        <div className="button-pair"><button className="secondary" disabled={busy || !connected} onClick={() => void controllers.run('Turning output on', async client => { const connection = controllers.require(client); await connection.setPower(true, controller.selectedOutput, storedPreset || undefined); await controllers.readProgramState(connection); })}>Turn on</button>
          <button className="secondary" disabled={busy || !connected} onClick={() => void controllers.run('Turning output off', async client => { const connection = controllers.require(client); await connection.setPower(false, controller.selectedOutput); await controllers.readProgramState(connection); })}>Turn off</button></div>
        <details><summary>Controller program storage</summary><p className="helper">Remove unused programs to free storage space. Programs referenced by presets, startup or running animations are protected.</p>
          {!writable && <p className="helper">This controller’s storage is read-only.</p>}
          {controller.description?.storage_bytes !== undefined && <p className="helper">{Math.floor((controller.description.free_bytes ?? 0) / 1024)} KiB free of {Math.floor(controller.description.storage_bytes / 1024)} KiB</p>}
          <label htmlFor="stored-program">Stored compiled program</label><select id="stored-program" value={storedProgram} onChange={event => setStoredProgram(event.target.value)}><option value="">Choose a program</option>{controller.programs.map(item => { const type = item.type ?? item.profile ?? 'animation'; return <option key={type + ':' + item.name} value={type + ':' + item.name}>{item.name} ({type})</option>; })}</select>
          <div className="button-pair"><button className="secondary" disabled={busy || !connected || !storedProgram} onClick={() => void controllers.run('Running stored program', async client => {
            const connection = controllers.require(client);
            if (!storedSelection) return;
            if ((storedSelection.type ?? storedSelection.profile) === 'transition') await connection.selectTransition(storedSelection.name, controller.selectedOutput);
            else { await connection.manage('program_activate', { name: storedSelection.name, output: controller.selectedOutput }); await controllers.readProgramState(connection); }
          })}>Run stored program</button><button className="quiet" disabled={busy || !controller.selectedPath || !storedProgram} onClick={() => void controllers.run('Downloading stored program', async client => {
            if (!storedSelection) return;
            const type = storedSelection.type ?? storedSelection.profile ?? 'animation';
            const data = await controllers.require(client).getProgram(storedSelection.name, type); download(new Uint8Array(data), storedSelection.name + '.' + type + '.lbc', 'application/octet-stream');
          })}>Download program</button></div>
          <button className="quiet full" disabled={busy || !writable || !controller.selectedPath || !storedProgram} onClick={() => void controllers.run('Deleting stored program', async client => { const connection = controllers.require(client); if (!storedSelection) return; await connection.deleteProgram(storedSelection.name, storedSelection.type ?? storedSelection.profile ?? 'animation'); await controllers.readPrograms(connection); setStoredProgram(''); })}>Delete stored program</button>
        </details>
      </section>
      {active && <TransitionControls />}
    </div>
  </>;
}
