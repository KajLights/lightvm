import { useEffect, useState } from 'react';
import { useRuntime } from './runtime';
import { useAppSelector } from './store';
import type { InputSource } from './types';

export function BoardStatus({ active }: { active: boolean }) {
  const { controllers } = useRuntime(), controller = useAppSelector(state => state.controller);
  const client = controllers.client;
  const [sources, setSources] = useState<InputSource[] | null>(null);
  const [error, setError] = useState('');
  useEffect(() => {
    let alive = true, reading = false;
    setSources(null); setError('');
    async function read() {
      if (!active || !client || client.closed || reading || document.hidden || controllers.snapshot.activity.operation) return;
      reading = true;
      try {
        const values = await client.json('input_list') as InputSource[];
        if (alive) { setSources(values.filter(value => value.origin === 'board')); setError(''); }
      } catch { if (alive) { setSources(null); setError('Board status unavailable.'); } }
      finally { reading = false; }
    }
    if (controller.description?.board_status_supported) void read();
    const timer = setInterval(() => { if (controller.description?.board_status_supported) void read(); }, 1000);
    return () => { alive = false; clearInterval(timer); };
  }, [active, client, controller.description?.boot_id, controller.description?.board_status_supported, controllers]);
  return <section className="card" aria-label="Board status"><h2>Board status</h2>
    {!controller.selectedPath ? <p>Connect a controller to read its sensors.</p> : !controller.description?.board_status_supported ? <p>This firmware does not expose board status.</p> : <>
      <p className="helper">These readings are available as sources in animation and transition input controls. Set the response range to choose how a reading affects the animation.</p>
      {error ? <p role="status">{error}</p> : sources === null ? <p role="status">Reading board status…</p> : <table><thead><tr><th>Measurement</th><th>Value</th><th>Source</th></tr></thead><tbody>{sources.map(source =>
        <tr key={source.name}><th scope="row">{source.label ?? source.name}</th><td>{source.valid && source.value !== null && source.age_ms !== null && source.age_ms <= 2000 ? typeof source.value === 'boolean' ? source.value ? 'Yes' : 'No' : `${source.value.toLocaleString(undefined, { maximumFractionDigits: 2 })}${source.unit ? ' ' + source.unit : ''}` : 'Unavailable'}</td><td><code>{source.name}</code></td></tr>)}</tbody></table>}
      <p className="helper">Chip temperature measures the silicon, not room temperature. Supply voltage appears only on boards with a measurement circuit.</p>
    </>}
  </section>;
}
