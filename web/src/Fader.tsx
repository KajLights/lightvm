import { useEffect, useMemo, useRef, useState, type CSSProperties, type DragEvent } from 'react';
import { Link } from 'react-router-dom';
import type { LightVMClient } from '../protocol.mjs';
import { useRuntime } from './runtime';
import { activityActions, useAppDispatch, useAppSelector } from './store';
import { programName, readLibrary, type DeviceLayer, type SavedAnimation, type SettingsValues } from './library';
import { prepareLibraryScene } from './library-device';
import { FADER_SOURCE, TransitionFaderInput } from './transition-fader';
import { TransitionProgram } from './TransitionProgram';
import type { Procedure, TransitionState } from './types';

export const ANIMATION_DRAG_TYPE = 'application/x-lightvm-animation';
type AnimationChoice = { key: string; label: string; detail: string } & (
  { animation: SavedAnimation; procedure?: never } | { procedure: Procedure; animation?: never });
function sceneSignature(scene: TransitionState['A']) {
  return JSON.stringify(scene.layers?.map(layer => [layer.id, layer.procedure, layer.generation]) ?? []);
}

export function Fader() {
  const { devices } = useRuntime(), dispatch = useAppDispatch();
  const device = useAppSelector(state => state.device), operation = useAppSelector(state => state.activity.operation);
  const [state, setState] = useState<TransitionState | null>(null);
  const [progress, setProgress] = useState(0), [owned, setOwned] = useState(false);
  const [duration, setDuration] = useState(2000), [interval, setIntervalMs] = useState(10000);
  const [saved] = useState(readLibrary);
  const [over, setOver] = useState<'A' | 'B' | null>(null), [loading, setLoading] = useState<'A' | 'B' | null>(null);
  const [slotLabels, setSlotLabels] = useState<Partial<Record<'A' | 'B', { label: string; signature: string }>>>({});
  const animations = useMemo(() => {
    const browser = saved.library.animations.filter(animation => animation.manifest.profile === 'animation');
    const known = new Set(browser.map(programName));
    return [
      ...browser.map(animation => ({ key: 'browser:' + animation.id, label: animation.name + ' v' + animation.version,
        detail: 'Saved in this browser', animation })),
      ...device.procedures.filter(procedure => (procedure.type ?? procedure.profile) === 'animation' && !known.has(procedure.name))
        .map(procedure => ({ key: 'device:' + procedure.name, label: procedure.name, detail: 'Saved on device', procedure })),
    ] as AnimationChoice[];
  }, [saved, device.procedures]);
  const animationNames = useMemo(() => {
    const names = new Map<string, Set<string>>();
    for (const animation of saved.library.animations) {
      const key = programName(animation);
      if (!names.has(key)) names.set(key, new Set());
      names.get(key)!.add(animation.name);
    }
    return new Map([...names].map(([key, values]) => [key, values.size === 1 ? [...values][0] : 'Saved animation']));
  }, [saved]);
  const output = device.selectedOutput, client = devices.client;
  const supported = Boolean(device.description?.ab_transitions && device.description.transition_sources);
  const active = useRef<TransitionFaderInput | null>(null), command = useRef(0);
  const input = useMemo(() => client && output ? new TransitionFaderInput(client, output,
    value => setState(previous => previous ? { ...previous, progress: value } : previous),
    error => {
      setOwned(false);
      if (error) { dispatch(activityActions.notice(error.message)); devices.log(error.message, 'error'); }
    }) : null, [client, output, device.description?.boot_id, devices, dispatch]);

  useEffect(() => {
    active.current = input; command.current++; setState(null); setProgress(0); setOwned(false); setSlotLabels({});
    let reading = false;
    async function read() {
      if (!input || !client || !output || !supported || reading || devices.snapshot.activity.operation || input.pending) return;
      reading = true;
      const revision = input.revision, epoch = command.current;
      try {
        const next = await client.transition(output) as TransitionState;
        if (active.current === input && epoch === command.current && revision === input.revision && !input.pending) {
          input.observe(next); setState(next); setProgress(next.progress);
        }
      } catch { /* Shared connection service reports disconnects. */ }
      finally { reading = false; }
    }
    void read(); const timer = setInterval(() => void read(), 500);
    return () => { active.current = null; clearInterval(timer); input?.stop(); };
  }, [input, client, output, supported, devices]);

  const busy = Boolean(operation) || !device.selectedPath || !client || !output || !state;
  const validDuration = Number.isFinite(duration) && duration >= 0;
  const validInterval = Number.isFinite(interval) && interval > 0 && interval >= duration;
  async function control(label: string, action: (connection: LightVMClient) => Promise<unknown>, claim = false) {
    if (!input || !output) return;
    input.stop(); setOwned(false); const epoch = ++command.current;
    await devices.run(label, async current => {
      const connection = devices.require(current);
      const result = await action(connection) as TransitionState | undefined;
      const next = await connection.transition(output) as TransitionState;
      if (active.current !== input || epoch !== command.current || devices.client !== connection || devices.snapshot.device.selectedOutput !== output) return;
      if (claim && result?.driver.assignment === next.driver.assignment && next.driver.kind === 'source' && next.driver.source === FADER_SOURCE) {
        input.attach(next.driver.assignment); setOwned(true);
      }
      setState(next); setProgress(next.progress);
    });
  }
  function slide(value: number) {
    if (!owned || busy || !Number.isFinite(value) || value < 0 || value > 100) return;
    setProgress(value); input?.publish(value);
  }
  async function loadAnimation(key: string, slot: 'A' | 'B') {
    const choice = animations.find(animation => animation.key === key);
    if (!choice || busy || !input || !output) return;
    setOver(null); setLoading(slot); const epoch = ++command.current;
    try {
      await devices.run('Loading animation into ' + slot, async current => {
        const connection = devices.require(current);
        const layers: DeviceLayer[] = choice.animation ? await prepareLibraryScene(connection, saved.library, [{
          id: 'main', animation: choice.animation.id, settings: { ...choice.animation.settings }, opacity: 255, blend: 'over',
        }]) : [{ id: 'main', procedure: choice.procedure.name, crc32: choice.procedure.crc32, settings: {}, opacity: 255, blend: 'over' }];
        const applied = await connection.configureScene(layers, output, slot);
        await devices.readProcedures(connection); await devices.readProgramState(connection);
        const revision = input.revision, next = await connection.transition(output) as TransitionState;
        if (active.current !== input || epoch !== command.current || devices.client !== connection || devices.snapshot.device.selectedOutput !== output) return;
        input.observe(next); setState(next);
        if (applied.value === next.revision) setSlotLabels(previous => ({ ...previous, [slot]: { label: choice.label, signature: sceneSignature(next[slot]) } }));
        if (revision === input.revision && !input.pending) setProgress(next.progress);
        devices.log('Loaded ' + choice.label + ' into scene ' + slot + '.', 'success');
      });
    } finally { setLoading(null); }
  }
  async function selectTransition(procedure: string, settings: SettingsValues) {
    if (busy || !input || !output) return;
    const epoch = ++command.current;
    await devices.run('Selecting transition', async current => {
      const connection = devices.require(current);
      await connection.selectTransition(procedure, output, { settings });
      const revision = input.revision, next = await connection.transition(output) as TransitionState;
      if (active.current !== input || epoch !== command.current || devices.client !== connection || devices.snapshot.device.selectedOutput !== output) return;
      input.observe(next); setState(next);
      if (revision === input.revision && !input.pending) setProgress(next.progress);
    });
  }
  function canDrop(event: DragEvent) { return !busy && Array.from(event.dataTransfer.types).includes(ANIMATION_DRAG_TYPE); }
  function drop(event: DragEvent, slot: 'A' | 'B') {
    event.preventDefault(); setOver(null);
    if (canDrop(event)) void loadAnimation(event.dataTransfer.getData(ANIMATION_DRAG_TYPE), slot);
  }
  const driver = owned ? 'Fader controls the transition' : state?.driver.kind === 'timed' ? 'Timed transition running'
    : state?.driver.kind === 'source' ? state.driver.source === FADER_SOURCE ? 'Fader released · Use fader to reconnect' : 'Controlled by ' + state.driver.source : 'Holding current position';
  const develop = '/devices/' + encodeURIComponent(device.description?.device_id ?? '') + '/develop';
  function sceneLabel(slot: 'A' | 'B') {
    const selected = slotLabels[slot];
    if (state && selected && selected.signature === sceneSignature(state[slot])) return selected.label;
    return state?.[slot].layers?.map(layer => animationNames.get(layer.procedure) ||
      (/^a[0-9a-f]{31}$/.test(layer.procedure) ? layer.id : layer.procedure)).join(' + ') || 'Empty scene';
  }

  return <>
    <div className="intro"><div><p className="eyebrow">FADER</p><h1>Move between your animations.</h1><p>Fade by hand or let the controller run a timed transition.</p></div>
      <Link className="quiet-link" to={develop}>Edit and save animations →</Link></div>
    {!supported ? <section className="card"><p>This device does not provide external transition inputs.</p></section> : <>
      <div className="fader-output"><label htmlFor="fader-output">Output</label>
        <select id="fader-output" value={output ?? ''} disabled={Boolean(operation) || !device.selectedPath} onChange={event => void devices.selectOutput(event.target.value)}>
          {!device.config?.devices.length && <option value="">Configure an output first</option>}
          {device.config?.devices.map(item => <option key={item.id}>{item.id}</option>)}
        </select></div>
      <div className="fader-layout">
        <section className="card fader-card" aria-label="Manual transition">
          <div className="card-heading"><h2>Manual fader</h2><span className={'badge ' + (owned ? 'ready' : '')}><span className="status-dot" />{owned ? 'Connected' : 'Released'}</span></div>
          <div className="fader-slots">{(['A', 'B'] as const).map(slot => <section key={slot}
            className={'fader-slot' + (over === slot ? ' drag-over' : '')} aria-labelledby={'fader-slot-' + slot} aria-busy={loading === slot}
            onDragOver={event => { if (canDrop(event)) { event.preventDefault(); event.dataTransfer.dropEffect = 'copy'; setOver(slot); } }}
            onDragLeave={event => { if (!event.currentTarget.contains(event.relatedTarget as Node | null)) setOver(null); }}
            onDrop={event => drop(event, slot)}>
            <h3 id={'fader-slot-' + slot}><span aria-hidden="true">{slot}</span>Scene {slot}</h3>
            <p className="fader-slot-name">{loading === slot ? 'Loading animation…' : sceneLabel(slot)}</p>
            <p className="helper">Drop a saved animation here to {state?.[slot].layers?.length ? 'replace this scene' : 'load this scene'}.</p>
          </section>)}</div>
          <label htmlFor="transition-fader">Transition fader</label>
          <output className="fader-value" htmlFor="transition-fader">{progress.toFixed(1)}<span>%</span></output>
          <input id="transition-fader" className="fader-range" type="range" min="0" max="100" step="0.1" value={progress}
            style={{ '--fader-progress': progress + '%' } as CSSProperties} disabled={busy || !owned}
            aria-valuetext={progress.toFixed(1) + '% towards scene B'} onChange={event => slide(Number(event.target.value))} />
          <div className="fader-endpoints"><span>A · 0%</span><span>B · 100%</span></div>
          <div className="fader-actions"><button className="primary" disabled={busy || owned} onClick={() => void control('Connecting fader', connection => connection.assignTransitionSource(FADER_SOURCE, output), true)}>Use fader</button>
            <button className="secondary" disabled={busy} onClick={() => void control('Holding transition', connection => connection.holdTransition(output))}>Hold</button></div>
          <p id="fader-driver" className="helper" role="status">{driver}</p>
          <p className="helper">Use fader takes over at the current position. Drag or use the arrow keys to move between A and B. Hold releases control at the current position.</p>
          {state?.error && <p className="connection-banner" role="alert">{state.error}</p>}
        </section>
        <div className="fader-sidebar">
        <section className="card" aria-label="Deployed transitions">
          <div className="card-heading"><h2>Transition</h2></div>
          <p className="muted">Choose how A changes into B for the fader and timed runs.</p>
          <TransitionProgram id="fader-transition-program" key={[device.selectedPath, output, device.description?.boot_id].join(':')} state={state} busy={busy} onApply={selectTransition} />
        </section>
        <section className="card fader-library" aria-label="Saved animations">
          <div className="card-heading"><h2>Saved animations</h2><span className="muted">{animations.length}</span></div>
          <p className="muted">Drag an animation onto A or B, or use its Load buttons.</p>
          {saved.error && <p className="helper" role="alert">{saved.error}</p>}
          {!animations.length && <p className="helper">Save an animation in Develop to add it here.</p>}
          <div className="fader-animation-list">{animations.map(animation => <article key={animation.key} aria-label={animation.label}
            className="fader-animation" draggable={!busy} aria-disabled={busy}
            onDragStart={event => {
              if (busy) { event.preventDefault(); return; }
              event.dataTransfer.effectAllowed = 'copy'; event.dataTransfer.setData(ANIMATION_DRAG_TYPE, animation.key);
            }} onDragEnd={() => setOver(null)}>
            <div><strong>{animation.label}</strong><p className="helper">{animation.detail}</p></div>
            <div className="button-pair">{(['A', 'B'] as const).map(slot => <button key={slot} className="secondary" disabled={busy}
              aria-label={'Load ' + animation.label + ' into ' + slot} onClick={() => void loadAnimation(animation.key, slot)}>Load {slot}</button>)}</div>
          </article>)}</div>
        </section>
        <section className="card fader-timed" aria-label="Timed transition">
          <div className="card-heading"><h2>Timed transition</h2></div>
          <p className="muted">Run from the current position, or repeat the demo between A and B.</p>
          <label htmlFor="fader-duration">Fade duration (ms)</label><input id="fader-duration" type="number" min="0" value={Number.isNaN(duration) ? '' : duration} onChange={event => setDuration(event.target.value === '' ? NaN : Number(event.target.value))} />
          <div className="button-pair"><button className="secondary" disabled={busy || !validDuration} onClick={() => void control('Transitioning to A', connection => connection.runTransition(0, duration, output))}>Run to A</button>
            <button className="secondary" disabled={busy || !validDuration} onClick={() => void control('Transitioning to B', connection => connection.runTransition(100, duration, output))}>Run to B</button></div>
          <label htmlFor="fader-interval">Repeat every (ms)</label><input id="fader-interval" type="number" min={Math.max(1, duration || 0)} value={Number.isNaN(interval) ? '' : interval} onChange={event => setIntervalMs(event.target.value === '' ? NaN : Number(event.target.value))} />
          <button className="primary full" disabled={busy || !validDuration || !validInterval} onClick={() => void control('Starting timed demo', async connection => {
            await connection.setProgress(0, output); await connection.runTransition(100, duration, output, interval);
          })}>Start timed demo</button>
          <p className="helper">Starts at A, fades to B, then reverses at each interval. The interval includes the fade and must be at least its duration. Playback continues when you leave this tab.</p>
        </section>
        </div>
      </div>
    </>}
  </>;
}
