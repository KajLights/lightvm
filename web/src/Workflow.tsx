import { VERSION } from './release';
import { useEffect, useRef, useState, type ReactNode } from 'react';
import { Link } from 'react-router-dom';
import type { LightVMClient } from '../protocol.mjs';
import { crc32 } from '../protocol.mjs';
import { supportsProgram, programCompatibility } from './compiler';
import { download } from './device-service';
import { useRuntime } from './runtime';
import { useAppSelector } from './store';
import type { Manifest } from './types';
import { BLENDS, bytesFor, deviceLayers, digestFor, importLibrary, programName, readLibrary, saveAnimation, settingsFor, writeLibrary,
  type Blend, type Library, type PresetLayer, type SavedAnimation, type SettingsValues } from './library';

function SettingFields({ manifest, values, onChange, prefix }: { manifest: Manifest; values: SettingsValues; onChange: (values: SettingsValues) => void; prefix: string }) {
  return <div className="setting-list">{(manifest.settings ?? []).map(setting => {
    const enabled = Object.hasOwn(values, setting.name), value = values[setting.name];
    const id = prefix + '-' + setting.name;
    return <div className="setting-row" key={setting.name}>
      <label className="setting-toggle"><input type="checkbox" checked={enabled} aria-label={'Override ' + setting.name} onChange={event => {
        const next = { ...values };
        if (event.target.checked) next[setting.name] = setting.type === 'bool' ? false : setting.minimum ?? 0;
        else delete next[setting.name];
        onChange(next);
      }} />Override {setting.name}</label>
      <label className="sr-only" htmlFor={id}>{setting.name}</label>
      {setting.type === 'bool' ? <select id={id} disabled={!enabled} value={enabled ? String(value) : 'default'} onChange={event => onChange({ ...values, [setting.name]: event.target.value === 'true' })}>
        {!enabled && <option value="default">Animation default</option>}<option value="true">True</option><option value="false">False</option>
      </select> : <input id={id} disabled={!enabled} type={setting.type === 'rgb8' && enabled ? 'color' : 'number'}
        value={!enabled ? '' : setting.type === 'rgb8' ? '#' + Number(value).toString(16).padStart(6, '0') : Number.isNaN(value) ? '' : String(value)}
        placeholder="Animation default" min={setting.minimum} max={setting.maximum} step={setting.type === 'float' ? 'any' : 1}
        onChange={event => onChange({ ...values, [setting.name]: setting.type === 'rgb8' ? parseInt(event.target.value.slice(1), 16) : event.target.value === '' ? NaN : Number(event.target.value) })} />}
    </div>;
  })}</div>;
}

export function DevelopmentWorkflow({ editorPanel, playback }: { editorPanel: ReactNode; playback: ReactNode }) {
  const { compiler, devices } = useRuntime();
  const editor = useAppSelector(state => state.editor), device = useAppSelector(state => state.device), activity = useAppSelector(state => state.activity);
  const [initial] = useState(readLibrary), [library, setLibrary] = useState(initial.library);
  const [storageError, setStorageError] = useState(initial.error);
  const [settings, setSettings] = useState<SettingsValues>({}), [animationName, setAnimationName] = useState('');
  const [selectedAnimation, setSelectedAnimation] = useState(''), [layers, setLayers] = useState<PresetLayer[]>([]);
  const [presetName, setPresetName] = useState(''), [selectedPreset, setSelectedPreset] = useState(''), [storedPreset, setStoredPreset] = useState('');
  const [storedProgram, setStoredProgram] = useState('');
  const picker = useRef<HTMLInputElement>(null);
  const busy = Boolean(activity.operation), output = device.config?.devices.find(item => item.id === device.selectedOutput);
  const connected = Boolean(device.selectedPath && output), artifact = editor.artifact;
  const compatible = Boolean(artifact && (!device.description || supportsProgram(device.description, artifact.manifest)));
  const selected = library.animations.find(item => item.id === selectedAnimation);
  // Example/file changes get their own defaults. Ordinary source edits retain overrides.
  const filename = useRef(editor.filename);
  useEffect(() => { if (filename.current !== editor.filename) { filename.current = editor.filename; setSettings({}); } }, [editor.filename]);
  useEffect(() => { setStoredPreset(''); setStoredProgram(''); }, [device.description?.device_id]);

  function commit(next: Library) {
    if (storageError) throw new Error(storageError);
    writeLibrary(next); setLibrary(next);
  }
  function single(animation: SavedAnimation) {
    setSelectedAnimation(animation.id);
    setLayers([{ id: 'layer1', animation: animation.id, settings: { ...animation.settings }, opacity: 255, blend: 'over' }]);
  }
  async function preview() {
    const compiled = compiler.artifact;
    if (!compiled || compiled.id !== devices.snapshot.editor.revision) return;
    let overrides: SettingsValues;
    try { overrides = settingsFor(compiled.manifest, settings); }
    catch (error) { await devices.run('Previewing animation', async () => { throw error; }); return; }
    const revision = editor.revision, outputId = output?.id;
    if (await devices.upload(compiled.bytecode, compiled.source, compiled.id, overrides) && devices.snapshot.editor.revision === revision && devices.snapshot.device.selectedOutput === outputId) {
      const resolved = devices.snapshot.device.programState;
      if (resolved && !resolved.layer) setSettings(resolved.settings_values ?? overrides);
    }
  }
  async function save() {
    await devices.run('Saving editable animation', async () => {
      const compiled = compiler.artifact;
      if (!compiled || compiled.id !== devices.snapshot.editor.revision) throw new Error('Wait for the latest compilation.');
      const saved = await saveAnimation(library, compiled, editor.filename, animationName, settings);
      commit(saved.library); setSelectedAnimation(saved.animation.id);
      if (layers.length <= 1) single(saved.animation);
      devices.log('Saved ' + saved.animation.name + ' v' + saved.animation.version + ' in this browser.', 'success');
    });
  }
  async function openAnimation() {
    if (!selected) return;
    compiler.edit(selected.source, selected.filename); filename.current = selected.filename;
    setSettings({ ...selected.settings }); setAnimationName(selected.name);
  }
  function addLayer() {
    if (!selected || layers.length >= 8) return;
    let n = 1; while (layers.some(layer => layer.id === 'layer' + n)) n++;
    setLayers([...layers, { id: 'layer' + n, animation: selected.id, settings: { ...selected.settings }, opacity: 255, blend: 'over' }]);
  }
  function patchLayer(index: number, patch: Partial<PresetLayer>) { setLayers(layers.map((layer, n) => n === index ? { ...layer, ...patch } : layer)); }
  function moveLayer(index: number, delta: number) { const next = [...layers]; [next[index], next[index + delta]] = [next[index + delta], next[index]]; setLayers(next); }
  async function ensurePrograms(client: LightVMClient) {
    const target = await client.describe();
    if (!target.scenes) throw new Error('Install firmware with scene composition support.');
    if (!layers.length || layers.length > target.scene_layer_limit) throw new Error('This output supports up to ' + target.scene_layer_limit + ' layers.');
    const animations = [...new Set(layers.map(layer => layer.animation))].map(id => library.animations.find(item => item.id === id)!);
    const stored = await client.json('procedure_list') as { name: string; size: number; crc32: number }[];
    const missing = animations.filter(animation => !stored.some(item => item.name === programName(animation)));
    if (stored.length + new Set(missing.map(programName)).size > target.procedure_limit) throw new Error('The device program library is full. Remove unused stored programs before adding these animation versions.');
    for (const animation of animations) {
      const bytes = bytesFor(animation);
      if (await digestFor(bytes) !== animation.digest) throw new Error('The saved animation checksum differs. Import an intact library backup.');
      const problem = programCompatibility(target, animation.manifest);
      if (problem) throw new Error(animation.name + ': ' + problem);
      if (bytes.length > target.module_limit) throw new Error(animation.name + ' exceeds this device’s program limit.');
      const match = stored.find(item => item.name === programName(animation));
      if (match && (match.crc32 !== crc32(bytes) || match.size !== bytes.length)) throw new Error('A stored animation version differs from the library.');
      if (!match) {
        await client.saveProcedure(programName(animation), bytes);
        stored.push({ name: programName(animation), size: bytes.length, crc32: crc32(bytes) });
      }
    }
    return deviceLayers(library, layers);
  }
  async function previewLayers() {
    await devices.run('Previewing preset', async client => {
      const connection = devices.require(client), outputId = device.selectedOutput;
      if (!outputId) throw new Error('Select an output first.');
      const recipe = await ensurePrograms(connection);
      await connection.configureScene(recipe, outputId); await devices.readProcedures(connection); await devices.readProgramState(connection);
      devices.log('Preset preview running on ' + outputId + '.', 'success');
    });
  }
  function checkedPreset() {
    if (!/^[A-Za-z0-9_.-]{1,30}$/.test(presetName)) throw new Error('Use 1–30 letters, digits, dots, underscores or hyphens for the preset name.');
    deviceLayers(library, layers);
    return { id: library.presets.find(item => item.name === presetName)?.id ?? crypto.randomUUID(), name: presetName, layers: structuredClone(layers) };
  }
  async function savePreset() {
    await devices.run('Saving preset', async () => {
      const preset = checkedPreset();
      commit({ ...library, presets: [...library.presets.filter(item => item.id !== preset.id), preset] });
      setSelectedPreset(preset.id); devices.log('Preset saved in this browser. Export the library for a backup.', 'success');
    });
  }
  async function saveToDevice() {
    await devices.run('Saving preset to device', async client => {
      checkedPreset(); const connection = devices.require(client), outputId = device.selectedOutput;
      if (!outputId) throw new Error('Select an output first.');
      const recipe = await ensurePrograms(connection);
      await connection.savePreset(presetName, recipe, outputId);
      await devices.readProcedures(connection); await devices.readPresets(connection);
      setStoredPreset(presetName); devices.log('Preset saved to device flash. Startup is configured separately.', 'success');
    });
  }
  async function importFile(file?: File) {
    if (!file) return;
    await devices.run('Importing animation library', async () => {
      if (file.size > 5 * 1024 * 1024) throw new Error('Choose a library smaller than 5 MiB.');
      const next = await importLibrary(library, await file.text());
      // A valid explicit import can recover an unreadable browser library.
      writeLibrary(next); setLibrary(next); setStorageError(''); devices.log('Library imported.', 'success');
    });
    if (picker.current) picker.current.value = '';
  }
  const layerBytes = output ? (8 * layers.length + 9) * output.config.count : null;
  return <>
    <div className="workspace">{editorPanel}<aside className="sidebar">
      <section className="card"><div className="card-heading"><h2>Preview on lights</h2></div>
        <label htmlFor="preview-output">Preview output</label><select id="preview-output" value={device.selectedOutput ?? ''} disabled={busy || !device.selectedPath} onChange={event => void devices.selectOutput(event.target.value)}>
          {!output && <option value="">Configure an output</option>}{device.config?.devices.map(item => <option key={item.id} value={item.id}>{item.id + ' · ' + item.config.pin + ' · ' + item.config.count + ' pixels'}</option>)}
        </select>
        {!output && device.selectedPath && <p className="helper"><Link to={'/devices/' + device.description?.device_id + '/configure'}>Configure an output</Link> before previewing.</p>}
        <p className="helper">Preview replaces playback on this output. Other outputs continue. Saved presets and startup settings stay as saved.</p>
        <div className="artifact"><span>Compiled animation</span><strong id="artifact-size">{artifact ? artifact.bytes.toLocaleString() + ' bytes' : 'Checking…'}</strong></div>
        {artifact && <p id="artifact-detail" className="helper">LightVM {artifact.manifest.requires}+ · {(artifact.manifest.settings ?? []).length} settings</p>}
        <fieldset disabled={busy}><SettingFields manifest={artifact?.manifest ?? { compiler_version: VERSION, requires: '1.0.0', schema: 'lightvm.program', profile: 'animation', pixels: { format: 'rgba8-premul' }, settings: [] }} values={settings} onChange={setSettings} prefix="draft" /></fieldset>
        <button id="upload" className="primary full" disabled={busy || !connected || !compatible || !artifact || artifact.bytes > (device.description?.module_limit ?? 0)} onClick={() => void preview()}>Preview animation →</button>
        {activity.progress !== null && <progress id="upload-progress" max={1} value={activity.progress} aria-label="Program upload progress" />}
        <div className="button-pair"><button id="download" className="quiet" disabled={!artifact} onClick={() => { if (compiler.artifact) download(new Uint8Array(compiler.artifact.bytecode), editor.filename.replace(/\.py$/i, '') + '.lbc', 'application/octet-stream'); }}>Download .lbc</button><button id="stop" className="quiet" disabled={busy || !connected} onClick={() => void devices.stopPlayback()}>Stop output</button></div>
        {artifact && !compatible && <p className="helper">{device.description && artifact && programCompatibility(device.description, artifact.manifest)}</p>}
        {editor.compiler === 'error' && <button id="retry-compiler" className="quiet" onClick={() => compiler.retry()}>Retry compiler</button>}
      </section>
      <section className="card"><div className="card-heading"><h2>Save animation</h2></div>
        <p className="helper">Save editable source, compiled code and selected defaults in this browser. Each save creates a version; existing presets keep theirs.</p>
        <label htmlFor="animation-name">Animation name</label><input id="animation-name" value={animationName} maxLength={80} onChange={event => setAnimationName(event.target.value)} />
        <button className="secondary full" disabled={busy || !artifact || !animationName.trim() || Boolean(storageError)} onClick={() => void save()}>Save animation</button>
      </section>{playback}
    </aside></div>
    <div className="workflow-grid">
      <section className="card"><div className="card-heading"><h2>Animation library</h2></div>
        <label htmlFor="saved-animation">Saved animation version</label><select id="saved-animation" value={selectedAnimation} onChange={event => setSelectedAnimation(event.target.value)}><option value="">Choose an animation</option>{library.animations.map(item => <option key={item.id} value={item.id}>{item.name + ' · v' + item.version}</option>)}</select>
        {selected && device.description && programCompatibility(device.description, selected.manifest) && <p role="status" className="helper">{programCompatibility(device.description, selected.manifest)} You can still edit and export this animation.</p>}
        <div className="button-pair"><button className="secondary" disabled={busy || !selected} onClick={() => void openAnimation()}>Edit animation</button><button className="quiet" disabled={busy || !selected} onClick={() => { if (selected) single(selected); }}>Use alone</button></div>
        <button className="secondary full" disabled={busy || !selected || layers.length >= 8} onClick={addLayer}>Add as layer</button>
        <button className="quiet full" disabled={busy || !selected || Boolean(storageError)} onClick={() => void devices.run('Deleting animation version', async () => {
          if (library.presets.some(preset => preset.layers.some(layer => layer.animation === selectedAnimation))) throw new Error('A saved browser preset uses this version. Delete or update that preset first.');
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
          </div>;
        })}</fieldset>
        {layerBytes !== null && layers.length > 0 && <p className="helper">Frame buffers: {layerBytes.toLocaleString()} bytes, plus animation state. The device checks available memory before replacing playback. Composition also stores each shared program in its library.</p>}
        <button className="primary full" disabled={busy || !connected || !layers.length || !device.description?.scenes} onClick={() => void previewLayers()}>Preview preset →</button>
        <label htmlFor="preset-name">Preset name</label><input id="preset-name" value={presetName} maxLength={30} onChange={event => setPresetName(event.target.value)} placeholder="blue-scanner" />
        <div className="button-pair"><button className="secondary" disabled={busy || !layers.length || !presetName || Boolean(storageError)} onClick={() => void savePreset()}>Save preset</button><button className="secondary" disabled={busy || !connected || !layers.length || !presetName || !device.description?.presets} onClick={() => void saveToDevice()}>Save preset to device</button></div>
        <p className="helper">Save preset keeps an editable recipe in this browser. Save to device stores its compiled programs and recipe in flash. Saving an existing name replaces that preset. Presets run on request; startup currently uses a single stored program.</p>
        {connected && !device.description?.presets && <p className="helper">Install firmware with preset storage support to save recipes on this device.</p>}
      </section>
      <section className="card"><div className="card-heading"><h2>Presets</h2></div>
        <label htmlFor="saved-preset">Browser preset</label><select id="saved-preset" value={selectedPreset} onChange={event => setSelectedPreset(event.target.value)}><option value="">Choose a preset</option>{library.presets.map(item => <option key={item.id} value={item.id}>{item.name}</option>)}</select>
        <button className="secondary full" disabled={busy || !selectedPreset} onClick={() => { const preset = library.presets.find(item => item.id === selectedPreset); if (preset) { setLayers(structuredClone(preset.layers)); setPresetName(preset.name); } }}>Open preset</button>
        <button className="quiet full" disabled={busy || !selectedPreset || Boolean(storageError)} onClick={() => void devices.run('Deleting browser preset', async () => {
          commit({ ...library, presets: library.presets.filter(item => item.id !== selectedPreset) }); setSelectedPreset('');
        })}>Delete browser preset</button>
        <hr /><label htmlFor="device-preset">Device preset</label><select id="device-preset" value={storedPreset} onChange={event => setStoredPreset(event.target.value)}><option value="">Choose a device preset</option>{device.presets.map(item => <option key={item.name}>{item.name}</option>)}</select>
        <div className="button-pair"><button className="secondary" disabled={busy || !connected || !storedPreset} onClick={() => void devices.run('Running device preset', async client => {
          const connection = devices.require(client); await connection.activatePreset(storedPreset, device.selectedOutput); await devices.readProgramState(connection); devices.log('Preset running on ' + device.selectedOutput + '.', 'success');
        })}>Run on output</button><button className="quiet" disabled={busy || !device.selectedPath || !storedPreset} onClick={() => void devices.run('Deleting device preset', async client => {
          const connection = devices.require(client); await connection.deletePreset(storedPreset); await devices.readPresets(connection); setStoredPreset('');
        })}>Delete device preset</button></div>
        <details><summary>Device program storage</summary><p className="helper">Remove unused programs to free library slots. Programs referenced by presets, startup or running animations are protected.</p>
          <label htmlFor="stored-program">Stored compiled program</label><select id="stored-program" value={storedProgram} onChange={event => setStoredProgram(event.target.value)}><option value="">Choose a program</option>{device.procedures.map(item => <option key={item.name}>{item.name}</option>)}</select>
          <div className="button-pair"><button className="secondary" disabled={busy || !connected || !storedProgram} onClick={() => void devices.run('Running stored program', async client => {
            const connection = devices.require(client); await connection.manage('procedure_activate', { name: storedProgram, device: device.selectedOutput }); await devices.readProgramState(connection);
          })}>Run stored program</button><button className="quiet" disabled={busy || !device.selectedPath || !storedProgram} onClick={() => void devices.run('Downloading stored program', async client => {
            const reply = await devices.require(client).manage('procedure_get', { name: storedProgram }); download(new Uint8Array(reply.data), storedProgram + '.lbc', 'application/octet-stream');
          })}>Download program</button></div>
          <button className="quiet full" disabled={busy || !device.selectedPath || !storedProgram} onClick={() => void devices.run('Deleting stored program', async client => { const connection = devices.require(client); await connection.deleteProcedure(storedProgram); await devices.readProcedures(connection); setStoredProgram(''); })}>Delete stored program</button>
        </details>
      </section>
    </div>
  </>;
}
