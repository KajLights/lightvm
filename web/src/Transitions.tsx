import { useEffect, useRef, useState } from 'react';
import type { LightVMClient } from '../protocol.mjs';
import { useRuntime } from './runtime';
import { useAppSelector } from './store';
import type { TransitionState } from './types';

export function TransitionControls() {
  const { devices } = useRuntime();
  const device = useAppSelector(state => state.device), operation = useAppSelector(state => state.activity.operation);
  const [state, setState] = useState<TransitionState | null>(null);
  const [progress, setProgress] = useState(0), [duration, setDuration] = useState(2000);
  const [presets, setPresets] = useState({ A: '', B: '' });
  const [effect, setEffect] = useState('');
  const output = device.selectedOutput, client = devices.client;
  const supported = Boolean(device.description?.ab_transitions), connected = Boolean(device.selectedPath && output);
  const observedEffect = useRef<string | null>(null);
  const sliderTimer = useRef<ReturnType<typeof setTimeout> | undefined>(undefined), editing = useRef(false);
  useEffect(() => {
    let active = true, polling = false;
    observedEffect.current = null; setState(null); setProgress(0); setPresets({ A: '', B: '' });
    async function read() {
      if (!active || !client || !supported || !output || polling || devices.snapshot.activity.operation) return;
      polling = true;
      try {
        const next = await client.transition(output) as TransitionState;
        if (active) { setState(next); if (observedEffect.current !== (next.procedure ?? '')) { observedEffect.current = next.procedure ?? ''; setEffect(next.procedure ?? ''); } if (!editing.current) setProgress(next.progress); }
      } catch { /* Connection errors are reported by the shared connection service. */ }
      finally { polling = false; }
    }
    void read(); const timer = setInterval(() => void read(), 500);
    return () => { active = false; clearInterval(timer); clearTimeout(sliderTimer.current); editing.current = false; };
  }, [client, output, supported, devices]);
  if (!supported) return null;
  const busy = Boolean(operation) || !connected;
  async function control(label: string, action: (connection: LightVMClient) => Promise<unknown>) {
    clearTimeout(sliderTimer.current); editing.current = false;
    await devices.run(label, async current => {
      const connection = devices.require(current);
      await action(connection);
      const next = await connection.transition(output) as TransitionState;
      if (devices.client === connection && devices.snapshot.device.selectedOutput === output) {
        setState(next); setProgress(next.progress); setEffect(next.procedure ?? '');
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
    <p className="helper">Prepare either scene while the other is visible. Endpoints retain both scenes and their animation state.</p>
    <div className="transition-controls-grid"><div>
    <div className="button-pair">{(['A', 'B'] as const).map(slot => <div key={slot}>
      <label htmlFor={'transition-slot-' + slot}>Scene {slot}</label>
      <select id={'transition-slot-' + slot} value={presets[slot]} onChange={event => setPresets({ ...presets, [slot]: event.target.value })}>
        <option value="">Choose a device preset</option>{device.presets.map(preset => <option key={preset.name}>{preset.name}</option>)}
      </select>
      <button className="secondary full" disabled={busy || !presets[slot]} onClick={() => void control('Preparing scene ' + slot, connection => connection.activatePreset(presets[slot], output, slot))}>Load {slot}</button>
      <p className="helper">{state?.[slot]?.active ? state[slot].layers?.length ? state[slot].layers!.length + (state[slot].layers!.length === 1 ? ' layer ready' : ' layers ready') : 'Empty scene' : 'Empty slot'}</p>
    </div>)}</div>
    <label htmlFor="transition-effect">Transition program</label>
    <select id="transition-effect" value={effect} onChange={event => setEffect(event.target.value)}>
      <option value="">Choose a stored transition</option>
      {device.procedures.filter(program => program.profile === 'transition').map(program => <option key={program.name}>{program.name}</option>)}
    </select>
    <button className="secondary" disabled={busy || !effect || !device.description?.transition_programs} onClick={() => void control('Selecting transition', connection => connection.selectTransition(effect, output))}>Apply transition</button>
    <p className="helper">Compile a transition example and save it to device flash to recall it here by name.</p>
    </div><div>
    <label htmlFor="transition-progress">Progress · {progress.toFixed(1)}%</label>
    <input id="transition-progress" type="range" min="0" max="100" step="0.1" value={progress} disabled={busy} onChange={event => slide(Number(event.target.value))} />
    <div className="button-pair"><button className="secondary" disabled={busy} onClick={() => void control('Cutting to A', connection => connection.setProgress(0, output))}>Cut to A</button>
      <button className="secondary" disabled={busy} onClick={() => void control('Cutting to B', connection => connection.setProgress(100, output))}>Cut to B</button></div>
    <label htmlFor="transition-duration">Duration (ms)</label><input id="transition-duration" type="number" min="0" value={duration} onChange={event => setDuration(Number(event.target.value))} />
    <div className="button-pair"><button disabled={busy || !Number.isFinite(duration) || duration < 0} onClick={() => void control('Transitioning to A', connection => connection.runTransition(0, duration, output))}>Run to A</button>
      <button disabled={busy || !Number.isFinite(duration) || duration < 0} onClick={() => void control('Transitioning to B', connection => connection.runTransition(100, duration, output))}>Run to B</button></div>
    <button className="secondary full" disabled={busy} onClick={() => void control('Holding transition', connection => connection.holdTransition(output))}>Hold</button>
    <p className="helper" role="status">Driver: {state?.driver.kind ?? 'manual'}{state?.driver.source ? ' · ' + state.driver.source : ''}</p>
    <p className="helper">Open the Fader workspace tab for continuous manual control through an external input.</p>
    </div></div>
    {state?.error && <p role="alert">{state.error}</p>}
  </section>;
}
