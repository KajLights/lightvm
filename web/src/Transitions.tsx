import { useEffect, useRef, useState } from 'react';
import type { LightVMClient } from '../protocol.mjs';
import { useRuntime } from './runtime';
import { useAppSelector } from './store';
import { TransitionProgram } from './TransitionProgram';
import { LiveInputs } from './LiveInputs';
import type { TransitionState } from './types';

export function TransitionControls() {
  const { controllers } = useRuntime();
  const controller = useAppSelector(state => state.controller), operation = useAppSelector(state => state.activity.operation);
  const [state, setState] = useState<TransitionState | null>(null);
  const [progress, setProgress] = useState(0), [duration, setDuration] = useState(2000);
  const [presets, setPresets] = useState({ A: '', B: '' });
  const [source, setSource] = useState('');
  const output = controller.selectedOutput, client = controllers.client;
  const supported = Boolean(controller.description?.ab_transitions), connected = Boolean(controller.selectedPath && output);
  const sliderTimer = useRef<ReturnType<typeof setTimeout> | undefined>(undefined), editing = useRef(false);
  useEffect(() => {
    let active = true, polling = false;
    setState(null); setProgress(0); setPresets({ A: '', B: '' });
    async function read() {
      if (!active || !client || !supported || !output || polling || controllers.snapshot.activity.operation) return;
      polling = true;
      try {
        const next = await client.transition(output) as TransitionState;
        if (active) { setState(next); if (!editing.current) setProgress(next.progress); }
      } catch { /* Connection errors are reported by the shared connection service. */ }
      finally { polling = false; }
    }
    void read(); const timer = setInterval(() => void read(), 500);
    return () => { active = false; clearInterval(timer); clearTimeout(sliderTimer.current); editing.current = false; };
  }, [client, output, supported, controllers]);
  if (!supported) return null;
  const busy = Boolean(operation) || !connected;
  async function control(label: string, action: (connection: LightVMClient) => Promise<unknown>) {
    clearTimeout(sliderTimer.current); editing.current = false;
    await controllers.run(label, async current => {
      const connection = controllers.require(current);
      await action(connection);
      const next = await connection.transition(output) as TransitionState;
      if (controllers.client === connection && controllers.snapshot.controller.selectedOutput === output) {
        setState(next); setProgress(next.progress);
      }
    });
  }
  function slide(value: number) {
    setProgress(value); editing.current = true; clearTimeout(sliderTimer.current);
    sliderTimer.current = setTimeout(() => {
      void control('Setting transition progress', connection => connection.setProgress(value, output)).finally(() => { editing.current = false; });
    }, 100);
  }
  return <section className="card transition-panel" aria-label="A/B transition">
    <div className="card-heading"><h2>A/B transition</h2></div>
    <p className="helper">Prepare either composition while the other is visible. Endpoints retain both compositions and their animation state. Slots stay fixed until you explicitly swap them.</p>
    <div className="transition-controls-grid"><div>
    <div className="button-pair">{(['A', 'B'] as const).map(slot => <div key={slot}>
      <label htmlFor={'transition-slot-' + slot}>Composition {slot}</label>
      <select id={'transition-slot-' + slot} value={presets[slot]} onChange={event => setPresets({ ...presets, [slot]: event.target.value })}>
        <option value="">Choose a controller preset</option>{controller.presets.map(preset => <option key={preset.name}>{preset.name}</option>)}
      </select>
      <button className="secondary full" disabled={busy || !presets[slot]} onClick={() => void control('Preparing composition ' + slot, connection => connection.activatePreset(presets[slot], output, slot))}>Load {slot}</button>
      <p className="helper">{state?.[slot]?.active ? state[slot].layers?.length ? state[slot].layers!.length + (state[slot].layers!.length === 1 ? ' layer ready' : ' layers ready') : 'Empty composition' : 'Empty slot'}</p>
    </div>)}</div>
    <TransitionProgram key={[controller.selectedPath, output, controller.description?.boot_id].join(':')} state={state} busy={busy || !state}
      onApply={(program, settings) => control('Selecting transition', connection => connection.selectTransition(program, output, { settings }))} />
    </div><div>
    <label htmlFor="transition-progress">Progress · {progress.toFixed(1)}%</label>
    <input id="transition-progress" type="range" min="0" max="100" step="0.1" value={progress} disabled={busy} onChange={event => slide(Number(event.target.value))} />
    <div className="button-pair"><button className="secondary" disabled={busy} onClick={() => void control('Cutting to A', connection => connection.setProgress(0, output))}>Cut to A</button>
      <button className="secondary" disabled={busy} onClick={() => void control('Cutting to B', connection => connection.setProgress(100, output))}>Cut to B</button></div>
    <label htmlFor="transition-duration">Duration (ms)</label><input id="transition-duration" type="number" min="0" value={duration} onChange={event => setDuration(Number(event.target.value))} />
    <div className="button-pair"><button disabled={busy || !Number.isFinite(duration) || duration < 0} onClick={() => void control('Transitioning to A', connection => connection.runTransition(0, duration, output))}>Run to A</button>
      <button disabled={busy || !Number.isFinite(duration) || duration < 0} onClick={() => void control('Transitioning to B', connection => connection.runTransition(100, duration, output))}>Run to B</button></div>
    <button className="secondary full" disabled={busy || (state?.progress !== 0 && state?.progress !== 100)} onClick={() => void control('Swapping A and B', connection => connection.swapTransition(output))}>Swap A and B</button>
    <button className="secondary full" disabled={busy} onClick={() => void control('Holding transition', connection => connection.holdTransition(output))}>Hold</button>
    <p className="helper" role="status">Driver: {state?.driver.kind ?? 'manual'}{state?.driver.source ? ' · ' + state.driver.source : ''}</p>
    <p className="helper">Open the Fader workspace tab for continuous manual control through an external input.</p>
    {controller.description?.live_inputs && <details><summary>Drive progress from a named input</summary><label htmlFor="transition-input-source">Progress signal (0–1)</label><input id="transition-input-source" value={source} maxLength={32} onChange={event => setSource(event.target.value)} />
      <button className="quiet full" disabled={busy || !state || !/^[A-Za-z0-9_.-]{1,32}$/.test(source)} onClick={() => void control('Binding transition progress', connection => connection.json('transition_bind', { output, expected_assignment: state!.driver.assignment, binding: { source } }))}>Bind progress source</button></details>}
    </div></div>
    {state?.error && <p role="alert">{state.error}</p>}
    {controller.description?.live_inputs && state?.program && <LiveInputs target="transition" />}
  </section>;
}
