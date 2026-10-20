import { useEffect, useMemo, useRef, useState, type CSSProperties, type DragEvent } from 'react';
import { Link } from 'react-router-dom';
import type { LightVMClient } from '../protocol.mjs';
import { useRuntime } from './runtime';
import { activityActions, useAppDispatch, useAppSelector } from './store';
import { type SettingsValues } from './program';
import { OutputLight } from './OutputLight';
import { LiveInputs } from './LiveInputs';
import { FADER_SOURCE, TransitionFaderInput } from './transition-fader';
import { TransitionProgram } from './TransitionProgram';
import type { TransitionState } from './types';

export const ANIMATION_DRAG_TYPE = 'application/x-lightvm-animation';
function compositionSignature(composition: TransitionState['A']) {
  return JSON.stringify(composition.layers?.map(layer => [layer.id, layer.program, layer.generation]) ?? []);
}

export function Fader() {
  const { controllers } = useRuntime(), dispatch = useAppDispatch();
  const controller = useAppSelector(state => state.controller), operation = useAppSelector(state => state.activity.operation);
  const [state, setState] = useState<TransitionState | null>(null);
  const [progress, setProgress] = useState(0), [owned, setOwned] = useState(false);
  const [duration, setDuration] = useState(2000), [interval, setIntervalMs] = useState(10000);
  const [over, setOver] = useState<'A' | 'B' | null>(null), [loading, setLoading] = useState<'A' | 'B' | null>(null);
  const [slotLabels, setSlotLabels] = useState<Partial<Record<'A' | 'B', { label: string; signature: string }>>>({});
  const animations = controller.programs.filter(program => (program.type ?? program.profile) === 'animation').map(program => ({ key: 'program:' + program.name, label: program.name, program }));
  const choices = [...controller.presets.map(preset => ({ key: 'preset:' + preset.name, label: preset.name, preset: preset.name, program: undefined })), ...animations.map(item => ({ ...item, preset: undefined }))];
  const output = controller.selectedOutput, client = controllers.client;
  const supported = Boolean(controller.description?.ab_transitions && controller.description.transition_sources);
  const active = useRef<TransitionFaderInput | null>(null), command = useRef(0);
  const input = useMemo(() => client && output ? new TransitionFaderInput(client, output,
    value => setState(previous => previous ? { ...previous, progress: value } : previous),
    error => {
      setOwned(false);
      if (error) { dispatch(activityActions.notice(error.message)); controllers.log(error.message, 'error'); }
    }) : null, [client, output, controller.description?.boot_id, controllers, dispatch]);

  useEffect(() => {
    active.current = input; command.current++; setState(null); setProgress(0); setOwned(false); setSlotLabels({});
    let reading = false;
    async function read() {
      if (!input || !client || !output || !supported || reading || controllers.snapshot.activity.operation || input.pending) return;
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
  }, [input, client, output, supported, controllers]);

  const busy = Boolean(operation) || !controller.selectedPath || !client || !output || !state;
  const validDuration = Number.isFinite(duration) && duration >= 0;
  const validInterval = Number.isFinite(interval) && interval > 0 && interval >= duration;
  async function control(label: string, action: (connection: LightVMClient) => Promise<unknown>, claim = false) {
    if (!input || !output) return;
    input.stop(); setOwned(false); const epoch = ++command.current;
    await controllers.run(label, async current => {
      const connection = controllers.require(current);
      const result = await action(connection) as TransitionState | undefined;
      const next = await connection.transition(output) as TransitionState;
      if (active.current !== input || epoch !== command.current || controllers.client !== connection || controllers.snapshot.controller.selectedOutput !== output) return;
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
    const choice = choices.find(animation => animation.key === key);
    if (!choice || busy || !input || !output) return;
    setOver(null); setLoading(slot); const epoch = ++command.current;
    try {
      await controllers.run('Loading animation into ' + slot, async current => {
        const connection = controllers.require(current);
        const applied = choice.preset
          ? await connection.activatePreset(choice.preset, output, slot)
          : await connection.configureComposition([{ id: 'main', program: choice.program!.name, crc32: choice.program!.crc32, settings: {}, opacity: 255, blend: 'over' }], output, slot);
        await controllers.readPrograms(connection); await controllers.readProgramState(connection);
        const revision = input.revision, next = await connection.transition(output) as TransitionState;
        if (active.current !== input || epoch !== command.current || controllers.client !== connection || controllers.snapshot.controller.selectedOutput !== output) return;
        input.observe(next); setState(next);
        if (applied.value === next.revision) setSlotLabels(previous => ({ ...previous, [slot]: { label: choice.label, signature: compositionSignature(next[slot]) } }));
        if (revision === input.revision && !input.pending) setProgress(next.progress);
        controllers.log('Loaded ' + choice.label + ' into composition ' + slot + '.', 'success');
      });
    } finally { setLoading(null); }
  }
  async function selectTransition(program: string, settings: SettingsValues) {
    if (busy || !input || !output) return;
    const epoch = ++command.current;
    await controllers.run('Selecting transition', async current => {
      const connection = controllers.require(current);
      await connection.selectTransition(program, output, { settings });
      const revision = input.revision, next = await connection.transition(output) as TransitionState;
      if (active.current !== input || epoch !== command.current || controllers.client !== connection || controllers.snapshot.controller.selectedOutput !== output) return;
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
    : state?.driver.kind === 'source' ? state.driver.source === FADER_SOURCE ? 'Fader released · Use fader to reconnect' : 'Controlled by ' + state.driver.source
    : state?.driver.kind === 'input' ? 'Controlled by an input binding' : 'Holding current position';
  const develop = controller.description ? '/controllers/' + encodeURIComponent(controller.description.controller_id) + '/editor' : '/editor';
  function compositionLabel(slot: 'A' | 'B') {
    const selected = slotLabels[slot];
    if (state && selected && selected.signature === compositionSignature(state[slot])) return selected.label;
    return state?.[slot].preset || state?.[slot].layers?.map(layer => layer.program).join(' + ') || 'Empty composition';
  }

  return <>
    <div className="intro"><div><p className="eyebrow">03 / MIXER</p><h1>Take the controls.</h1><p>Cue a preset. Choose a transition. Make your next move.</p></div>
      <div className="fader-output"><label htmlFor="fader-output">Output</label><select id="fader-output" value={output ?? ''} disabled={Boolean(operation) || !controller.selectedPath} onChange={event => void controllers.selectOutput(event.target.value)}>
        {!output && <option value="">Choose an output</option>}{controller.config?.outputs.filter(item => item.mode !== 'indicators').map(item => <option key={item.id}>{item.id}</option>)}</select></div></div>
    <OutputLight />
    {!supported ? <section className="card"><p>{controller.description ? 'This controller needs A/B transition firmware to use the Mixer.' : 'Connect a device to cue presets and mix your lights.'}</p><Link to="/controllers">Connect a device →</Link></section> : <>
      <div className="mixing-desk">
        <div className="desk-heading"><span className="desk-label">LIGHTVM / PERFORMANCE DESK</span><span className={'badge ' + (controller.selectedPath ? 'ready' : '')}><span className="status-dot" />{controller.selectedPath ? 'LIVE CONTROL' : 'DISCONNECTED'}</span></div>
        <div className="bus-grid">{(['A', 'B'] as const).map(slot => {
          const amount = slot === 'A' ? 100 - progress : progress;
          return <section key={slot} className={'fader-slot bus bus-' + slot.toLowerCase() + (over === slot ? ' drag-over' : '')} aria-labelledby={'fader-slot-' + slot} aria-busy={loading === slot}
            onDragOver={event => { if (canDrop(event)) { event.preventDefault(); event.dataTransfer.dropEffect = 'copy'; setOver(slot); } }}
            onDragLeave={event => { if (!event.currentTarget.contains(event.relatedTarget as Node | null)) setOver(null); }} onDrop={event => drop(event, slot)}>
            <div className="bus-title"><h2 id={'fader-slot-' + slot}><span aria-hidden="true">{slot}</span>Composition {slot}</h2><span className={'bus-tally ' + (amount > 0 ? 'on-air' : '')}>{amount > 0 ? 'ON AIR' : 'READY'}</span></div>
            <p className="fader-slot-name">{loading === slot ? 'Loading…' : compositionLabel(slot)}</p>
            <div className="bus-level"><meter aria-label={'Bus ' + slot + ' mix contribution'} min="0" max="100" value={amount} /><output>{amount.toFixed(0)}%</output></div>
            <p className="helper">Mix position · transition effects may reveal pixels differently.</p>
            <div className="preset-bank" aria-label={'Bus ' + slot + ' presets'}>{controller.presets.map((preset, index) => <button key={preset.name} className={'cue-key' + (state?.[slot].preset === preset.name ? ' selected' : '')} disabled={busy} aria-label={'Load ' + preset.name + ' into ' + slot} onClick={() => void loadAnimation('preset:' + preset.name, slot)}><small>{String(index + 1).padStart(2, '0')}</small>{preset.name}</button>)}</div>
            {!controller.presets.length && <p className="helper">Create presets in Configuration to fill this bus.</p>}
          </section>;
        })}</div>
        <div className="desk-main">
          <section className="crossfader-module" aria-label="Manual transition">
            <div className="card-heading"><h2>A/B crossfader</h2><span className={'badge ' + (owned ? 'ready' : '')}>{owned ? 'MANUAL' : state?.driver.kind === 'timed' ? 'AUTO' : 'HOLD'}</span></div>
            <label htmlFor="transition-fader">Transition fader</label><output className="fader-value" htmlFor="transition-fader">{progress.toFixed(1)}<span>% B</span></output>
            <input id="transition-fader" className="fader-range" type="range" min="0" max="100" step="0.1" value={progress} style={{ '--fader-progress': progress + '%' } as CSSProperties} disabled={busy || !owned} aria-valuetext={progress.toFixed(1) + '% towards composition B'} onChange={event => slide(Number(event.target.value))} />
            <div className="fader-endpoints"><span>A</span><span>25</span><span>50</span><span>75</span><span>B</span></div>
            <div className="transport-keys"><button className="cut-key" disabled={busy} onClick={() => void control('Cutting to A', connection => connection.setProgress(0, output))}>Cut to A</button><button className="cut-key" disabled={busy} onClick={() => void control('Cutting to B', connection => connection.setProgress(100, output))}>Cut to B</button>
              <button className="auto-key" disabled={busy || !validDuration} onClick={() => void control('Transitioning to A', connection => connection.runTransition(0, duration, output))}>Run to A</button><button className="auto-key" disabled={busy || !validDuration} onClick={() => void control('Transitioning to B', connection => connection.runTransition(100, duration, output))}>Run to B</button></div>
            <div className="fader-actions"><button className="primary" disabled={busy || owned} onClick={() => void control('Connecting fader', connection => connection.assignTransitionSource(FADER_SOURCE, output), true)}>Use fader</button><button className="secondary" disabled={busy} onClick={() => void control('Holding transition', connection => connection.holdTransition(output))}>Hold</button><button className="secondary" disabled={busy || (state?.progress !== 0 && state?.progress !== 100)} onClick={() => void control('Swapping A and B', connection => connection.swapTransition(output))}>Swap A and B</button></div>
            <p id="fader-driver" className="helper" role="status">{driver}</p><p className="helper">Swap at either end of the fader to keep the visible output continuous.</p>
            {state?.error && <p className="connection-banner" role="alert">{state.error}</p>}
          </section>
          <section className="transition-module" aria-label="Deployed transitions"><h2>Transition / AUTO</h2>
            {state ? <TransitionProgram id="fader-transition-program" key={[controller.selectedPath, output, controller.description?.boot_id].join(':')} state={state} busy={busy} onApply={selectTransition} /> : <p className="helper" role="status">Reading active transition…</p>}
            <label htmlFor="fader-duration">Fade duration (ms)</label><input id="fader-duration" type="number" min="0" value={Number.isNaN(duration) ? '' : duration} onChange={event => setDuration(event.target.value === '' ? NaN : Number(event.target.value))} />
            <details><summary>Repeat A ↔ B</summary><label htmlFor="fader-interval">Repeat every (ms)</label><input id="fader-interval" type="number" min={Math.max(1, duration || 0)} value={Number.isNaN(interval) ? '' : interval} onChange={event => setIntervalMs(event.target.value === '' ? NaN : Number(event.target.value))} />
              <button className="secondary full" disabled={busy || !validDuration || !validInterval} onClick={() => void control('Starting timed loop', async connection => { await connection.setProgress(0, output); await connection.runTransition(100, duration, output, interval); })}>Start timed loop</button><p className="helper">Repeats on the device until you take control or hold.</p></details>
          </section>
        </div>
        <div className="desk-footer"><span>OUTPUT / {output}</span><div><button disabled={busy} onClick={() => void control('Turning output on', connection => connection.setPower(true, output))}>On</button><button className="blackout-key" disabled={busy} onClick={() => void control('Turning output off', connection => connection.setPower(false, output))}>Fade to black</button></div></div>
      </div>
      <details className="card device-programs"><summary>Deployed animations · {animations.length}</summary><p className="helper">Load an animation with its defaults, or drag it onto either bus.</p>
        <div className="fader-animation-list">{animations.map(animation => <article key={animation.key} aria-label={animation.label} className="fader-animation" draggable={!busy} onDragStart={event => { if (busy) { event.preventDefault(); return; } event.dataTransfer.effectAllowed = 'copy'; event.dataTransfer.setData(ANIMATION_DRAG_TYPE, animation.key); }} onDragEnd={() => setOver(null)}><strong>{animation.label}</strong><div className="button-pair">{(['A', 'B'] as const).map(slot => <button key={slot} disabled={busy} aria-label={'Load ' + animation.label + ' into ' + slot} onClick={() => void loadAnimation(animation.key, slot)}>Load {slot}</button>)}</div></article>)}</div><Link to={develop}>Open Editor →</Link>
      </details>
      {controller.description?.live_inputs && <details className="card device-programs"><summary>Live animation inputs</summary><LiveInputs />{state?.program && <LiveInputs target="transition" />}</details>}
    </>}
  </>;
}
