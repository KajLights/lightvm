import { useEffect, useRef, useState } from 'react';
import { assetUrl } from './compiler';
import { useRuntime } from './runtime';
import { useAppSelector } from './store';
import type { PreviewSnapshot } from './preview-engine';
import type { Compilation, Diagnostic } from './types';
import { settingsFor, inputsFor } from './program';
import { usePreviewRecipe } from './preview-recipe';

export function LocalPreview() {
  const recipe = usePreviewRecipe();
  const { compiler } = useRuntime(), source = useAppSelector(s => s.editor.source), ready = useAppSelector(s => s.editor.compiler === 'ready');
  const [count, setCount] = useState(50), [ring, setRing] = useState(false), [stepping, setStepping] = useState(false);
  const [snapshot, setSnapshot] = useState<PreviewSnapshot>(), [artifact, setArtifact] = useState<Compilation>();
  const [busy, setBusy] = useState(false), [playing, setPlaying] = useState(false), [message, setMessage] = useState('');
  const [delta, setDelta] = useState(0.033), [watch, setWatch] = useState('');
  const worker = useRef<Worker | null>(null), serial = useRef(0), generation = useRef(0), running = useRef(false);
  const last = useRef<PreviewSnapshot | undefined>(undefined), timer = useRef<ReturnType<typeof setTimeout> | undefined>(undefined);
  const requests = useRef(new Map<number, { resolve: (s: PreviewSnapshot) => void; reject: (e: Error) => void }>());
  const stop = () => { running.current = false; setPlaying(false); clearTimeout(timer.current); };
  const reset = () => {
    stop(); generation.current++; worker.current?.terminate(); worker.current = null;
    for (const request of requests.current.values()) request.reject(new Error('The preview was reset.'));
    requests.current.clear(); last.current = undefined; setSnapshot(undefined); setArtifact(undefined); setBusy(false); setMessage('');
  };
  useEffect(() => { reset(); }, [source, count, stepping, recipe.settings]);
  useEffect(() => () => { running.current = false; generation.current++; clearTimeout(timer.current); worker.current?.terminate(); }, []);
  const request = (data: object) => new Promise<PreviewSnapshot>((resolve, reject) => {
    if (!worker.current) { reject(new Error('Start the preview first.')); return; }
    const id = ++serial.current; requests.current.set(id, { resolve, reject }); worker.current.postMessage({ id, ...data });
  });
  function accept(result: PreviewSnapshot) {
    last.current = result; setSnapshot(result);
    if (result.error) { setMessage(result.error.message); stop(); }
  }
  async function start() {
    reset(); const current = generation.current; setBusy(true); setMessage('Starting your lights…');
    try {
      const compiled = await compiler.preview(stepping);
      if (generation.current !== current) return;
      if (compiled.diagnostics.length) { setMessage(compiled.diagnostics[0].message); return; }
      setArtifact(compiled);
      const vm = new Worker(new URL('./preview-worker.ts', import.meta.url), { type: 'module' }); worker.current = vm;
      vm.onmessage = ({ data }) => {
        const pending = requests.current.get(data.id); if (!pending) return;
        requests.current.delete(data.id); data.error ? pending.reject(new Error(data.error)) : pending.resolve(data.snapshot);
      };
      vm.onerror = event => {
        for (const pending of requests.current.values()) pending.reject(new Error(event.message || 'The preview could not load. Rebuild the editor preview assets.'));
        requests.current.clear();
      };
      const inputs = inputsFor(compiled.manifest, recipe.inputs);
      accept(await request({ type: 'open', baseUrl: assetUrl('./'), artifact: compiled, count, seed: 1, settings: settingsFor(compiled.manifest, recipe.settings), inputs }));
      if (!last.current?.error) {
        if (stepping) accept(await request({ type: 'advance', step: true }));
        else await finishFrame(current);
      }
      if (generation.current === current && !last.current?.error) setMessage('Ready. Change a value in your code, or try another frame.');
    } catch (error) { if (generation.current === current) setMessage((error as Error).message); }
    finally { if (generation.current === current) setBusy(false); }
  }
  async function finishFrame(current: number) {
    do {
      const result = await request({ type: 'advance', step: false });
      if (generation.current !== current) return;
      accept(result); if (result.phase >= 2) return;
      await new Promise(resolve => setTimeout(resolve, 0));
    } while (generation.current === current);
  }
  async function advance(step: boolean) {
    if (!last.current || busy) return;
    const current = generation.current; setBusy(true);
    try {
      if (last.current.phase === 2) accept(await request({ type: 'frame', delta }));
      if (step) accept(await request({ type: 'advance', step: true }));
      else await finishFrame(current);
    } catch (error) { if (generation.current === current) { setMessage((error as Error).message); stop(); } }
    finally { if (generation.current === current) setBusy(false); }
  }
  async function play() {
    if (running.current) { stop(); return; }
    running.current = true; setPlaying(true); const current = generation.current;
    const tick = async () => {
      if (!running.current || generation.current !== current) return;
      try {
        if (last.current?.phase === 2) accept(await request({ type: 'frame', delta }));
        await finishFrame(current);
      } catch (error) { if (generation.current === current) setMessage((error as Error).message); stop(); }
      if (running.current && generation.current === current) timer.current = setTimeout(tick, Math.max(16, delta * 1000));
    };
    void tick();
  }
  function show(location: Diagnostic | NonNullable<PreviewSnapshot['location']>) {
    window.dispatchEvent(new CustomEvent(location.blockId ? 'lightvm-focus-block' : 'lightvm-focus-source', { detail: location.blockId ?? location }));
  }
  const available = Object.keys(snapshot?.values ?? {}), selected = available.includes(watch) ? watch : available[0];
  return <section className="local-preview" aria-label="Local light preview">
    <h2>Try it here</h2><p className="helper">No controller needed. Pixel 0 is the first light. Only a finished frame replaces the picture.</p>
    <div className="preview-controls">
      <label>Pixels <input aria-label="Preview pixel count" type="number" min="1" max="512" value={count} onChange={e => { const n = Number(e.target.value); if (Number.isInteger(n) && n >= 1 && n <= 512) setCount(n); }} /></label>
      <label><input type="checkbox" checked={ring} onChange={e => setRing(e.target.checked)} /> Show a ring</label>
      <label><input type="checkbox" checked={stepping} onChange={e => setStepping(e.target.checked)} /> Step through the code</label>
      <button disabled={!ready || busy} onClick={() => void start()}>{snapshot ? 'Restart' : 'Start preview'}</button>
      <button disabled={!snapshot || snapshot.phase === 3 || busy && !playing} onClick={() => void play()}>{playing ? 'Pause' : 'Play'}</button>
      <button disabled={!snapshot || snapshot.phase === 3 || busy || playing} onClick={() => void advance(false)}>Next frame</button>
      {stepping && <button disabled={!snapshot || snapshot.phase === 3 || busy || playing} onClick={() => void advance(true)}>Next step</button>}
      <button className="quiet" disabled={!snapshot && !busy} onClick={reset}>Stop and clear</button>
    </div>
    <svg role="img" aria-label={`${count} preview pixels in a ${ring ? 'ring' : 'strip'}`} viewBox={ring ? '0 0 340 340' : `0 0 ${Math.max(240, count * 14)} 60`} className={ring ? 'preview-ring' : 'preview-strip'}>
      {Array.from({ length: count }, (_, i) => {
        const angle = i / count * 2 * Math.PI - Math.PI / 2, p = snapshot?.pixels;
        const fill = p ? `rgb(${p[i * 4]}, ${p[i * 4 + 1]}, ${p[i * 4 + 2]})` : '#15212b';
        return <circle key={i} cx={ring ? 170 + 140 * Math.cos(angle) : 10 + i * 14} cy={ring ? 170 + 140 * Math.sin(angle) : 25} r={ring ? Math.min(9, 330 / count) : 5} fill={fill} stroke={i === 0 ? '#fff' : '#647683'} strokeWidth={i === 0 ? 2 : 0.5}><title>Pixel {i}</title></circle>;
      })}
      <text x={ring ? 170 : 6} y={ring ? 170 : 54} textAnchor={ring ? 'middle' : 'start'} fill="currentColor" fontSize="12">{ring ? '0 starts at the top →' : '0 →'}</text>
    </svg>
    <p role="status">{message}</p>
    {snapshot && <p className="helper">Time: {snapshot.seconds.toFixed(3)} seconds. {snapshot.phase < 2 ? 'This frame is still being drawn.' : snapshot.phase === 2 ? 'Frame finished.' : 'Stopped at a problem.'}</p>}
    <label>Seconds per frame <select value={delta} onChange={e => { stop(); setDelta(Number(e.target.value)); }}><option value={0}>0 — no time passes</option><option value={0.016}>0.016</option><option value={0.033}>0.033</option><option value={0.25}>0.25 — a delay</option><option value={1}>1 — a long delay</option></select></label>
    {snapshot?.error && <button className="diagnostic" onClick={() => show(snapshot.error!)}>Show the problem {snapshot.error.blockId ? 'block' : `on line ${snapshot.error.line}`}</button>}
    {stepping && snapshot?.location && !snapshot.error && <button className="quiet" onClick={() => show(snapshot.location!)}>Show {snapshot.location.blockId ? 'this block' : `line ${snapshot.location.line}`}</button>}
    {stepping && available.length > 0 && <p><label>Watch a value <select value={selected} onChange={e => setWatch(e.target.value)}>{available.map(name => <option key={name}>{name}</option>)}</select></label> <output aria-label="Watched value">{String(snapshot?.values[selected])}</output></p>}
    {!!artifact?.manifest.inputs?.length && <p className="helper">These values take effect on the next frame and are kept when you save locally. Timers and device sources use the starting value here; choose a value below to try a change.</p>}
    {artifact?.manifest.inputs?.map((input, index) => {
      const binding = recipe.inputs[input.name], value = binding && 'value' in binding ? binding.value : input.default;
      return <label className="preview-input" key={input.name}>{input.name} <input type={input.type === 'bool' ? 'checkbox' : 'number'} step={input.type === 'float' ? 'any' : 1} min={input.minimum} max={input.maximum} checked={Boolean(value)} value={Number(value)} disabled={!snapshot || snapshot.phase !== 2 || playing || busy} onChange={event => {
        const next = input.type === 'bool' ? event.target.checked : Number(event.target.value);
        void request({ type: 'input', index, value: Number(next) }).then(result => { recipe.setInputs({ ...recipe.inputs, [input.name]: { value: next } }); accept(result); }).catch(error => setMessage(error.message));
      }} /></label>;
    })}
  </section>;
}
