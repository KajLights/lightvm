import { useEffect, useRef, useState } from 'react';
import { useRuntime } from './runtime';
import { useAppSelector } from './store';
import type { OutputLightState } from './types';

type Patch = Partial<Pick<OutputLightState, 'brightness' | 'color' | 'power'>>;
export function OutputLight() {
  const { controllers } = useRuntime();
  const controller = useAppSelector(state => state.controller);
  const [light, setLight] = useState<OutputLightState | null>(null), [error, setError] = useState('');
  const publish = useRef<(patch: Patch) => void>(() => {});
  const client = controllers.client, output = controller.selectedOutput;
  useEffect(() => {
    let alive = true, sending = false, reading = false, version = 0;
    let current: OutputLightState | null = null, pending: Patch = {}, timer: ReturnType<typeof setTimeout> | undefined;
    setLight(null); setError('');
    async function read() {
      if (!alive || !client || !output || sending || reading || Object.keys(pending).length || !controller.description?.light_controls) return;
      const started = version; reading = true;
      try {
        const next = await client.json('output_light_get', { output }) as OutputLightState;
        if (alive && started === version) { current = next; setLight(next); }
      } catch (problem) { if (alive) setError((problem as Error).message); }
      finally { reading = false; }
    }
    async function send() {
      timer = undefined;
      if (!alive || !client || !current || sending || !Object.keys(pending).length) return;
      const patch = pending; pending = {}; sending = true;
      try {
        const next = await client.json('output_light_set', { output, expected_revision: current.revision,
          ...('power' in patch ? { expected_composition_revision: current.composition_revision } : {}), ...patch }) as OutputLightState;
        if (alive) { current = next; setLight({ ...next, ...pending }); setError(''); }
      } catch (problem) {
        if (alive) { pending = {}; setError((problem as Error).message); }
      } finally {
        sending = false;
        if (alive && Object.keys(pending).length) timer = setTimeout(() => void send(), 60);
        else void read();
      }
    }
    publish.current = patch => {
      if (!current || !alive) return;
      ++version; pending = { ...pending, ...patch }; setLight(previous => previous ? { ...previous, ...patch } : previous);
      if (!sending && timer === undefined) timer = setTimeout(() => void send(), 60);
    };
    void read(); const poll = setInterval(() => void read(), 750);
    return () => { alive = false; clearTimeout(timer); clearInterval(poll); publish.current = () => {}; };
  }, [client, output, controller.description?.boot_id, controller.description?.light_controls]);
  if (!controller.description?.light_controls) return null;
  return <section className="card output-light" aria-label="Light controls"><div className="card-heading"><h2>Light</h2><span className="helper">{output}</span></div>
    <fieldset disabled={!light || !client}>
      <label className="checkbox-label"><input type="checkbox" aria-label="Light power" checked={light?.power ?? false} onChange={event => publish.current({ power: event.target.checked })} /> On</label>
      <label>Brightness <output>{Math.round((light?.brightness ?? 1) * 100)}%</output><input aria-label="Light brightness" type="range" min="0" max="1" step="0.01" value={light?.brightness ?? 1} onChange={event => publish.current({ brightness: event.target.valueAsNumber })} /></label>
      <label>Colour<input aria-label="Light colour" type="color" value={'#' + (light?.color ?? 0xffffff).toString(16).padStart(6, '0')} onChange={event => publish.current({ color: parseInt(event.target.value.slice(1), 16) })} /></label>
    </fieldset>
    {light && !light.color_control && <p className="helper">Colour is saved for animations that follow the light colour.</p>}
    <p className="helper">Brightness dims the whole output, including music and paused animations.</p>
    {error && <p role="alert">{error}</p>}
  </section>;
}
