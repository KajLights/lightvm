import { useEffect, useMemo, useRef, useState } from 'react';
import { useRuntime } from './runtime';
import { activityActions, controllerActions, useAppDispatch, useAppSelector } from './store';
import { LiveInputSession } from './live-input-session';
import { InputBindingControl } from './InputBindingControl';
import type { InputBindings, InputSource, ProgramState } from './types';

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
    current.current = session; let reading = false, alive = true, sourcesRead = 0;
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
        if (alive && Date.now() - sourcesRead >= 2000) {
          sourcesRead = Date.now();
          const available = await client.json('input_list') as InputSource[];
          if (alive && current.current === session) setSources(available);
        }
      } catch { if (alive && scoped) setRunning(null); }
      finally { reading = false; }
    }
    const timer = setInterval(() => void read(), 500);
    if (scoped) void read();
    if (session && client) void client.json('input_list').then((next: InputSource[]) => { if (alive && current.current === session) { setSources(next); sourcesRead = Date.now(); } }).catch(() => {});
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
      return input && <InputBindingControl sources={sources} key={state!.generation + ':' + declaration.name} declaration={declaration} binding={input.binding} fallback={input.fallback} value={values[declaration.name] ?? input.value} publish={binding => {
        if ('value' in binding) setValues(previous => ({ ...previous, [declaration.name]: binding.value }));
        session?.publish({ [declaration.name]: binding });
      }} />;
    })}</fieldset>
    <datalist id="live-input-sources">{sources.map(source => <option key={source.name} value={source.name}>{source.type}</option>)}</datalist>
    {onCapture && state?.inputs && <button className="quiet full" disabled={!session || session.pending || busy} onClick={() => onCapture(Object.fromEntries(Object.entries(state.inputs!).map(([name, input]) => [name, input.binding])), state)}>Use these inputs in draft</button>}
  </section>;
}
