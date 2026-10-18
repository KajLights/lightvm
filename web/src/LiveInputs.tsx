import { useEffect, useMemo, useRef, useState } from 'react';
import { useRuntime } from './runtime';
import { activityActions, controllerActions, useAppDispatch, useAppSelector } from './store';
import { LiveInputSession } from './live-input-session';
import type { InputBinding, InputBindings, InputDeclaration, InputSource, InputState, ProgramState, RampDriver } from './types';

function InputControl({ declaration, state, value, publish }: {
  declaration: InputDeclaration; state: InputState; value: number | boolean; publish: (binding: InputBinding) => void;
}) {
  const binding = state.binding, name = declaration.name;
  const bindingKey = JSON.stringify(binding);
  const [selected, setSelected] = useState('manual');
  const [duration, setDuration] = useState('2'), [mode, setMode] = useState<RampDriver['mode']>('pingpong');
  const [source, setSource] = useState('');
  useEffect(() => {
    setSelected('driver' in binding ? 'timed' : 'source' in binding ? 'source' : 'manual');
    if ('driver' in binding) { setDuration(String(binding.driver.duration)); setMode(binding.driver.mode); }
    if ('source' in binding) setSource(binding.source);
  }, [bindingKey]);
  function timer() {
    const seconds = Number(duration);
    if (Number.isFinite(seconds) && seconds > 0 && seconds <= 1e9) publish({ driver: {
      kind: 'ramp', duration: seconds, mode, easing: declaration.driver?.easing ?? 'linear',
      from: declaration.minimum ?? 0, to: declaration.maximum ?? 1,
    } });
  }
  const min = declaration.minimum, max = declaration.maximum;
  const bounded = typeof min === 'number' && typeof max === 'number' && max > min;
  const step = declaration.type === 'int' ? 1 : bounded ? (max - min) / 1000 : 'any';
  return <div className="live-input-control">
    <div className="live-input-heading"><strong>{name}</strong><output aria-label={name + ' current value'}>{typeof value === 'boolean' ? String(value) : Number(value.toFixed(3))}{declaration.unit ? ' ' + declaration.unit : ''}</output></div>
    {declaration.type === 'bool' ? <label className="checkbox-label"><input type="checkbox" aria-label={name + ' value'} checked={Boolean(value)} onChange={event => publish({ value: event.target.checked })} /> Enabled</label>
      : declaration.type === 'rgb8' ? <input type="color" aria-label={name + ' value'} value={'#' + Number(value).toString(16).padStart(6, '0')} onChange={event => publish({ value: parseInt(event.target.value.slice(1), 16) })} />
      : <input type={bounded ? 'range' : 'number'} aria-label={name + ' value'} min={min} max={max} step={step} value={Number(value)} onChange={event => {
        const next = event.target.valueAsNumber;
        if (Number.isFinite(next) && (min === undefined || next >= min) && (max === undefined || next <= max)) publish({ value: next });
      }} />}
    <div className="live-input-actions"><label className="sr-only" htmlFor={'input-driver-' + name}>{name + ' driver'}</label><select id={'input-driver-' + name} value={selected} onChange={event => {
      setSelected(event.target.value);
      if (event.target.value === 'manual') publish({ value });
      else if (event.target.value === 'timed') timer();
      else if (source.trim()) publish({ source: source.trim() });
      else setSource('controls.' + name);
    }}>
      <option value="manual">Manual</option>{declaration.type === 'float' && <option value="timed">Timed</option>}<option value="source">Named source</option>
    </select><button className="quiet small" onClick={() => publish({ default: true })}>Restore default</button></div>
    {declaration.type === 'float' && <details open={selected === 'timed'}><summary>Timer</summary><div className="live-input-actions">
      <label>Seconds per traversal<input type="number" aria-label={name + ' timer duration'} min="0.001" max="1000000000" step="any" value={duration} onChange={event => setDuration(event.target.value)} /></label>
      <label>Motion<select aria-label={name + ' timer mode'} value={mode} onChange={event => setMode(event.target.value as RampDriver['mode'])}><option value="once">Once</option><option value="repeat">Repeat</option><option value="pingpong">Bounce</option></select></label>
    </div><button className="quiet small" onClick={timer} disabled={!Number.isFinite(Number(duration)) || Number(duration) <= 0 || Number(duration) > 1e9}>Apply timer</button></details>}
    <details open={selected === 'source'}><summary>Source binding</summary><label>Signal name<input aria-label={name + ' source'} list="live-input-sources" value={source} maxLength={32} onChange={event => setSource(event.target.value)} placeholder={'controls.' + name} /></label>
      <button className="quiet small" disabled={!/^[A-Za-z0-9_.-]{1,32}$/.test(source)} onClick={() => publish({ source })}>Bind source</button>
      {state.fallback && <p className="helper">Source unavailable; using the declared default.</p>}
    </details>
  </div>;
}

export function LiveInputs({ active = true, target, onCapture }: { active?: boolean; target?: 'transition'; onCapture?: (bindings: InputBindings, state: ProgramState) => void }) {
  const { controllers } = useRuntime(), dispatch = useAppDispatch();
  const controller = useAppSelector(state => state.controller), busy = useAppSelector(state => Boolean(state.activity.operation));
  const [selected, setSelected] = useState(''), [running, setRunning] = useState<ProgramState | null>(null);
  const [targets, setTargets] = useState<{ value: string; label: string }[]>([]);
  const scoped = Boolean(target || selected);
  const state = scoped ? running : controller.programState, declarations = state?.manifest.inputs ?? [];
  const client = controllers.client;
  const [values, setValues] = useState<Record<string, number | boolean>>({}), [sources, setSources] = useState<InputSource[]>([]);
  const [epoch, setEpoch] = useState(0);
  const current = useRef<LiveInputSession | null>(null);
  useEffect(() => { setSelected(''); setRunning(null); setTargets([]); }, [client, controller.selectedOutput, target]);
  useEffect(() => {
    let alive = true;
    if (!target && active && client && controller.selectedOutput && controller.description?.live_inputs) {
      void client.transition(controller.selectedOutput).then(next => {
        if (!alive) return;
        const choices = (['A','B'] as const).flatMap(slot => (next[slot]?.layers ?? []).filter((layer: { id: string }) => slot === 'B' || controller.programState?.layer || layer.id !== 'main')
          .map((layer: { id: string }) => ({ value: slot + ':' + layer.id, label: slot + ' · ' + layer.id })));
        setTargets(choices);
      }).catch(() => {});
    }
    return () => { alive = false; };
  }, [client, target, active, controller.selectedOutput, controller.programState?.generation, controller.description?.live_inputs]);
  function changed(next: ProgramState) { if (scoped) setRunning(next); else dispatch(controllerActions.programState(next)); }
  const session = useMemo(() => client && active && state?.inputs && declarations.length ? new LiveInputSession(client,
    { output: state.output, slot: state.slot, layer: state.layer, target: state.target, generation: state.generation }, state.inputs_revision ?? 0,
    next => changed(next), error => {
      dispatch(activityActions.notice(error.message)); controllers.log(error.message, 'error');
      setEpoch(previous => previous + 1);
      if (!client.closed) void controllers.readProgramState(client).catch(() => {});
  }) : null, [client, active, epoch, scoped, selected, target, state?.output, state?.slot, state?.layer, state?.generation, declarations.length, controller.description?.boot_id, controllers, dispatch]);
  useEffect(() => {
    current.current = session; let reading = false, alive = true;
    const fields = target ? { output: controller.selectedOutput, target } : selected ? { output: controller.selectedOutput, slot: selected.slice(0,1), layer: selected.slice(2) }
      : state && { output: state.output, slot: state.slot, layer: state.layer };
    async function read() {
      if ((!session && !scoped) || !active || !controller.description?.live_inputs || !client || !fields || reading || session?.pending || controllers.snapshot.activity.operation) return;
      const version = session?.version; reading = true;
      try {
        const next = await client.json('program_state', fields) as ProgramState;
        if (alive && current.current === session && version === session?.version && !session?.pending) {
          session?.observe(next); changed(next);
        }
      } catch { if (alive && scoped) setRunning(null); }
      finally { reading = false; }
    }
    const timer = setInterval(() => void read(), 500);
    if (scoped) void read();
    if (session && client) void client.json('input_list').then((next: InputSource[]) => { if (current.current === session) setSources(next); }).catch(() => {});
    return () => { alive = false; current.current = null; clearInterval(timer); session?.stop(); };
  }, [session, client, active, selected, target, scoped, controller.selectedOutput, controller.description?.live_inputs, controllers, dispatch]);
  useEffect(() => {
    if (session && state && !session.pending) {
      session.observe(state);
      setValues(Object.fromEntries(Object.entries(state.inputs ?? {}).map(([name, input]) => [name, 'value' in input.binding ? input.binding.value : input.value])));
    }
  }, [state, session]);
  if ((!declarations.length || !state?.inputs) && (target || !targets.length)) return null;
  return <section className="card live-inputs" aria-label={target ? 'Live transition inputs' : 'Live animation inputs'}><div className="card-heading"><h2>{target ? 'Transition inputs' : 'Live inputs'}</h2><span className="helper">{state?.layer ?? state?.output}</span></div>
    {!target && targets.length > 0 && <><label htmlFor="live-input-target">Animation to control</label><select id="live-input-target" value={selected} onChange={event => { setSelected(event.target.value); setRunning(null); }}><option value="">Current animation</option>{targets.map(item => <option key={item.value} value={item.value}>{item.label}</option>)}</select></>}
    {!declarations.length && <p className="helper">This animation has no declared live inputs.</p>}
    <p className="helper">Drag a value to take manual control. Timers and bindings keep the same animation running.</p>
    <fieldset disabled={!session || !controller.selectedPath || busy || state?.faulted}>{declarations.map(declaration => {
      const input = state!.inputs![declaration.name];
      return input && <InputControl key={state!.generation + ':' + declaration.name} declaration={declaration} state={input} value={values[declaration.name] ?? input.value} publish={binding => {
        if ('value' in binding) setValues(previous => ({ ...previous, [declaration.name]: binding.value }));
        session?.publish({ [declaration.name]: binding });
      }} />;
    })}</fieldset>
    <datalist id="live-input-sources">{sources.map(source => <option key={source.name} value={source.name}>{source.type}</option>)}</datalist>
    {onCapture && state?.inputs && <button className="quiet full" disabled={!session || session.pending || busy} onClick={() => onCapture(Object.fromEntries(Object.entries(state.inputs!).map(([name, input]) => [name, input.binding])), state)}>Use these inputs in draft</button>}
  </section>;
}
