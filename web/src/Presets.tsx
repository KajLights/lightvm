import { useEffect, useState } from 'react';
import { useRuntime } from './runtime';
import { useAppSelector } from './store';
import { BLENDS, programManifest, settingsFor, inputsFor, validTransitionSpec, type CompositionLayer } from './program';
import { SettingFields } from './SettingFields';
import { InputRecipeFields } from './InputRecipeFields';
import { RecallTransition } from './RecallTransition';
import { crc32 } from '../protocol.mjs';
import type { Manifest, TransitionSpec } from './types';

function LayerSettings({ layer, patch, onValidity }: { layer: CompositionLayer; patch: (change: Partial<CompositionLayer>) => void; onValidity: (valid: boolean) => void }) {
  const { controllers } = useRuntime(), client = controllers.client;
  const [manifest, setManifest] = useState<Manifest>(), [error, setError] = useState('');
  useEffect(() => {
    let active = true; setManifest(undefined); setError(''); onValidity(false);
    if (client) void (async () => {
      try {
        const bytes = await client.getProgram(layer.program, 'animation');
        if (crc32(bytes) !== layer.crc32) throw new Error('This program changed. Select the animation again.');
        const next = programManifest(bytes);
        if (next.profile !== 'animation') throw new Error('Choose an animation for this layer.');
        settingsFor(next, layer.settings); inputsFor(next, layer.inputs ?? {});
        if (active) { setManifest(next); onValidity(true); }
      } catch (problem) { if (active) setError((problem as Error).message); }
    })();
    return () => { active = false; };
  }, [client, layer.program, layer.crc32]);
  return <>{error && <p role="alert">{error}</p>}{manifest && <>
    <SettingFields manifest={manifest} values={layer.settings} onChange={settings => patch({ settings })} prefix={'preset-' + layer.id} />
    <InputRecipeFields manifest={manifest} values={layer.inputs ?? {}} onChange={inputs => patch({ inputs })} onValidity={onValidity} prefix={'preset-' + layer.id} />
  </>}</>;
}

export function Presets() {
  const { controllers } = useRuntime(), controller = useAppSelector(state => state.controller);
  const busy = useAppSelector(state => Boolean(state.activity.operation));
  const [selected, setSelected] = useState(''), [name, setName] = useState('');
  const [layers, setLayers] = useState<CompositionLayer[]>([]), [transition, setTransition] = useState<TransitionSpec>();
  const [revision, setRevision] = useState<number>(), [invalid, setInvalid] = useState<Record<string, boolean>>({});
  const [dirty, setDirty] = useState(false);
  const programs = controller.programs.filter(program => (program.type ?? program.profile) === 'animation');
  const connected = Boolean(controller.selectedPath && controller.selectedOutput), writable = controller.description?.storage_writable !== false;
  function patch(index: number, change: Partial<CompositionLayer>) { setLayers(current => current.map((layer, n) => n === index ? { ...layer, ...change } : layer)); setDirty(true); }
  async function open() {
    await controllers.run('Opening preset', async client => {
      const connection = controllers.require(client), state = await connection.describe();
      const recipe = await connection.json('preset_get', { name: selected });
      setName(selected); setLayers(structuredClone(recipe.layers)); setTransition(recipe.entry_transition); setRevision(state.catalog_revision); setInvalid({}); setDirty(false);
    });
  }
  function add() {
    const program = programs[0]; if (!program) return;
    let n = 1; while (layers.some(layer => layer.id === 'layer' + n)) n++;
    const id = 'layer' + n;
    setLayers([...layers, { id, program: program.name, crc32: program.crc32, settings: {}, opacity: 255, blend: 'over' }]);
    setInvalid(current => ({ ...current, [id]: true })); setDirty(true);
  }
  const valid = /^[A-Za-z0-9_.-]{1,32}$/.test(name) && layers.length > 0 && layers.every(layer => !invalid[layer.id]) && (!transition || validTransitionSpec(transition));
  async function save() {
    await controllers.run('Saving preset', async client => {
      if (!valid) throw new Error('Check the preset name, layers and transition.');
      const connection = controllers.require(client);
      for (const layer of layers) {
        const bytes = await connection.getProgram(layer.program, 'animation');
        if (crc32(bytes) !== layer.crc32) throw new Error('A layer program changed. Select its animation again.');
        const manifest = programManifest(bytes); settingsFor(manifest, layer.settings); inputsFor(manifest, layer.inputs ?? {});
      }
      const result = await connection.savePreset(name, layers, controller.selectedOutput, transition, revision);
      setRevision(result.value >>> 0); setSelected(name); setDirty(false); await controllers.readPresets(connection);
      controllers.log('Saved preset ' + name + '. Run it from Mixer or choose it for startup.', 'success');
    });
  }
  return <div className="preset-layout">
    <section className="card"><h2>Device presets</h2><p className="helper">A preset combines deployed animations, their settings and a transition.</p>
      <label htmlFor="preset-output">Output</label><select id="preset-output" value={controller.selectedOutput ?? ''} disabled={busy || !controller.selectedPath} onChange={event => void controllers.selectOutput(event.target.value)}>{!controller.selectedOutput && <option value="">Configure an output first</option>}{controller.config?.outputs.filter(item => item.mode !== 'indicators').map(item => <option key={item.id}>{item.id}</option>)}</select>
      <label htmlFor="controller-preset">Preset</label><select id="controller-preset" value={selected} onChange={event => setSelected(event.target.value)}><option value="">Choose a preset</option>{controller.presets.map(item => <option key={item.name}>{item.name}</option>)}</select>
      <div className="button-pair"><button disabled={busy || !connected || !selected} onClick={() => void open()}>Open preset</button><button className="quiet" disabled={busy} onClick={() => { setSelected(''); setName(''); setLayers([]); setTransition(undefined); setRevision(undefined); setInvalid({}); setDirty(false); }}>New preset</button></div>
      <button className="quiet" disabled={busy || !connected || !selected || !writable} onClick={() => void controllers.run('Deleting preset', async client => {
        const connection = controllers.require(client); await connection.deletePreset(selected); await controllers.readPresets(connection); setSelected('');
      })}>Delete preset</button>
      <p className="helper">Deploy animations and transitions from Editor. Choose power-on behavior under Output &amp; startup.</p>
    </section>
    <section className="card composer"><div className="card-heading"><h2>Preset setup</h2><span className="helper" role="status">{dirty ? 'Unsaved changes' : 'Device recipe'}</span></div>
      <fieldset disabled={busy || !connected || !writable}>
        <label htmlFor="preset-name">Preset name</label><input id="preset-name" value={name} maxLength={32} placeholder="evening-glow" onChange={event => { setName(event.target.value); setDirty(true); }} />
        <p className="helper">Layers run from bottom to top.</p>
        {layers.map((layer, index) => <section className="layer-card" key={layer.id} aria-label={'Layer ' + (index + 1)}>
          <div className="card-heading"><h3>Layer {index + 1}</h3><div className="layer-actions">
            <button className="quiet" aria-label={'Move layer ' + (index + 1) + ' down'} disabled={index === 0} onClick={() => { const next = [...layers]; [next[index - 1], next[index]] = [next[index], next[index - 1]]; setLayers(next); setDirty(true); }}>↓</button>
            <button className="quiet" aria-label={'Move layer ' + (index + 1) + ' up'} disabled={index === layers.length - 1} onClick={() => { const next = [...layers]; [next[index + 1], next[index]] = [next[index], next[index + 1]]; setLayers(next); setDirty(true); }}>↑</button>
            <button className="quiet" aria-label={'Remove layer ' + (index + 1)} onClick={() => { setLayers(layers.filter((_, n) => n !== index)); setDirty(true); }}>×</button>
          </div></div>
          <label htmlFor={'preset-animation-' + layer.id}>Animation</label><select id={'preset-animation-' + layer.id} value={layer.program} onChange={event => {
            const program = programs.find(item => item.name === event.target.value)!;
            setInvalid(current => ({ ...current, [layer.id]: true })); patch(index, { program: program.name, crc32: program.crc32, settings: {}, inputs: {} });
          }}>{!programs.some(item => item.name === layer.program) && <option value={layer.program}>{layer.program} (unavailable)</option>}{programs.map(item => <option key={item.name}>{item.name}</option>)}</select>
          <div className="layer-options"><label>Opacity<input aria-label={'Layer ' + (index + 1) + ' opacity'} type="range" min="0" max="255" value={layer.opacity} onChange={event => patch(index, { opacity: Number(event.target.value) })} /></label>
            <label>Blend<select aria-label={'Layer ' + (index + 1) + ' blend'} value={layer.blend} onChange={event => patch(index, { blend: event.target.value as CompositionLayer['blend'] })}>{BLENDS.map(blend => <option key={blend}>{blend}</option>)}</select></label></div>
          <details><summary>Settings &amp; inputs</summary><LayerSettings layer={layer} patch={change => patch(index, change)} onValidity={value => setInvalid(previous => Boolean(previous[layer.id]) === !value ? previous : { ...previous, [layer.id]: !value })} /></details>
        </section>)}
        {!programs.length && <p className="helper">Deploy an animation in Editor to create your first preset.</p>}
        <button className="secondary" disabled={!programs.length || layers.length >= (controller.description?.composition_layer_limit ?? 8)} onClick={add}>+ Add layer</button>
        <RecallTransition id="preset-entry-transition" label="Preset transition" value={transition} onChange={value => { setTransition(value); setDirty(true); }} />
        <p className="helper">Used for preset selection, ON, OFF and startup.</p>
        <button className="primary" disabled={!valid || !controller.description?.presets} onClick={() => void save()}>Save preset to device</button>
      </fieldset>
    </section>
  </div>;
}
