import { useEffect, useState } from 'react';
import type { InputBinding, InputDeclaration, InputSource, RampDriver } from './types';

export const MUSIC_SIGNALS = ['level', 'bass', 'mid', 'treble', 'centroid', 'onset'] as const;
export const MUSIC_LABELS = ['Level', 'Bass', 'Midrange', 'Treble', 'Spectral centre', 'Onset pulse'];
export function defaultBinding(input: InputDeclaration): InputBinding {
  return input.control ? { control: input.control } : input.driver ? { driver: input.driver } : { value: input.default };
}
export function InputBindingControl({ declaration, binding, value, fallback = false, sources = [], publish }: {
  declaration: InputDeclaration; binding: InputBinding; value: number | boolean; fallback?: boolean;
  sources?: InputSource[]; publish: (binding: InputBinding) => void;
}) {
  const name = declaration.name, numeric = declaration.type === 'float' || declaration.type === 'int';
  const effective = 'default' in binding ? defaultBinding(declaration) : binding;
  const key = JSON.stringify(effective);
  const sourceNames = sources.map(signal => signal.name).join(',');
  const [selected, setSelected] = useState('manual'), [source, setSource] = useState('');
  const [duration, setDuration] = useState(2), [mode, setMode] = useState<RampDriver['mode']>('pingpong');
  const [low, setLow] = useState(0), [high, setHigh] = useState(1);
  const [from, setFrom] = useState(0), [to, setTo] = useState(1);
  useEffect(() => {
    setSelected('control' in effective ? 'control:' + effective.control : 'driver' in effective ? 'timed' :
      'source' in effective ? MUSIC_SIGNALS.some(signal => effective.source === 'music.' + signal) ? effective.source : sources.some(signal => signal.name === effective.source) ? effective.source : 'source' : 'manual');
    if ('driver' in effective) { setDuration(effective.driver.duration); setMode(effective.driver.mode); }
    if ('source' in effective) {
      setSource(effective.source); setLow(effective.map?.input_min ?? effective.palette?.input_min ?? 0); setHigh(effective.map?.input_max ?? effective.palette?.input_max ?? 1);
      setFrom(effective.map?.output_min ?? effective.palette?.from ?? declaration.minimum ?? 0);
      setTo(effective.map?.output_max ?? effective.palette?.to ?? declaration.maximum ?? 1);
    }
  }, [key, sourceNames]);
  const bounded = declaration.minimum !== undefined && declaration.maximum !== undefined && declaration.maximum > declaration.minimum;
  function timer() {
    if (Number.isFinite(duration) && duration > 0 && duration <= 1e9) publish({ driver: { kind: 'ramp', duration, mode,
      easing: declaration.driver?.easing ?? 'linear', from: declaration.minimum ?? 0, to: declaration.maximum ?? 1 } });
  }
  function selectSource(next: string, music = false) {
    if (!next) return;
    const signal = sources.find(signal => signal.name === next);
    const sourceType = music ? 'float' : signal?.type;
    const input_min = signal?.suggested_min ?? 0, input_max = signal?.suggested_max ?? 1;
    const numericSource = sourceType === 'float' || sourceType === 'int';
    if ('source' in effective && (effective.map || effective.palette) && sourceType !== 'rgb8' && sourceType !== 'bool') { publish({ ...effective, source: next }); return; }
    publish({ source: next, ...(signal?.origin === 'board' && numericSource && numeric ? { map: { input_min, input_max, output_min: declaration.minimum ?? 0, output_max: declaration.maximum ?? 1 } } : music && numeric ? { map: { input_min: 0, input_max: 1, output_min: declaration.minimum ?? 0, output_max: declaration.maximum ?? 1 } } :
      numericSource && declaration.type === 'rgb8' ? { palette: { input_min, input_max, from: 0x0044ff, to: 0xff4400 } } :
      numericSource && numeric && sourceType !== declaration.type ? { map: { input_min: 0, input_max: 1, output_min: declaration.minimum ?? 0, output_max: declaration.maximum ?? 1 } } : {}) });
  }
  const isSource = 'source' in effective;
  const mapping = isSource && (numeric || Boolean(effective.palette));
  const validRange = [low, high, from, to].every(Number.isFinite) && low < high &&
    (declaration.type !== 'int' || (Number.isInteger(from) && Number.isInteger(to))) &&
    (declaration.minimum === undefined || Math.min(from, to) >= declaration.minimum) &&
    (declaration.maximum === undefined || Math.max(from, to) <= declaration.maximum);
  return <div className="live-input-control">
    <div className="live-input-heading"><strong>{name}</strong><output aria-label={name + ' current value'}>{typeof value === 'boolean' ? String(value) : declaration.type === 'rgb8' ? '#' + Number(value).toString(16).padStart(6, '0') : Number(value.toFixed(3))}{declaration.unit ? ' ' + declaration.unit : ''}</output></div>
    {declaration.type === 'bool' ? <label className="checkbox-label"><input type="checkbox" aria-label={name + ' value'} checked={Boolean(value)} onChange={event => publish({ value: event.target.checked })} /> Enabled</label>
      : declaration.type === 'rgb8' ? <input type="color" aria-label={name + ' value'} value={'#' + Number(value).toString(16).padStart(6, '0')} onChange={event => publish({ value: parseInt(event.target.value.slice(1), 16) })} />
      : <input type={bounded ? 'range' : 'number'} aria-label={name + ' value'} min={declaration.minimum} max={declaration.maximum} step={declaration.type === 'int' ? 1 : bounded ? (declaration.maximum! - declaration.minimum!) / 1000 : 'any'} value={Number(value)} onChange={event => { if (Number.isFinite(event.target.valueAsNumber)) publish({ value: event.target.valueAsNumber }); }} />}
    <label>What controls {name}?<select aria-label={name + ' driver'} value={selected} onChange={event => {
      const next = event.target.value; setSelected(next);
      if (next === 'manual') publish({ value });
      else if (next === 'timed') timer();
      else if (next === 'control:color' || next === 'control:brightness') publish({ control: next === 'control:color' ? 'color' : 'brightness' });
      else if (next.startsWith('music.')) selectSource(next, true);
      else if (next !== 'source') selectSource(next);
    }}><option value="manual">Fixed value</option>
      {declaration.type === 'rgb8' && <option value="control:color">Light colour</option>}
      {declaration.type === 'float' && (declaration.minimum ?? 0) <= 0 && (declaration.maximum ?? 1) >= 1 && <option value="control:brightness">Light brightness (behaviour)</option>}
      {declaration.type === 'float' && <option value="timed">Timer</option>}
      {(numeric || declaration.type === 'rgb8') && MUSIC_SIGNALS.map((signal, index) => <option key={signal} value={'music.' + signal}>Music → {MUSIC_LABELS[index]}</option>)}
      {sources.filter(signal => !signal.name.startsWith('music.') && (signal.type === declaration.type || ((signal.type === 'float' || signal.type === 'int') && (numeric || declaration.type === 'rgb8')))).map(signal => <option key={signal.name} value={signal.name}>{signal.origin === 'board' ? `Board → ${signal.label ?? signal.name}${signal.unit ? ` (${signal.unit})` : ''}` : signal.name}</option>)}
      <option value="source">Named source…</option>
    </select></label>
    <button className="quiet small" onClick={() => publish({ default: true })}>Restore default</button>
    {selected === 'timed' && <div className="live-input-actions"><label>Seconds per traversal<input type="number" aria-label={name + ' timer duration'} min="0.001" max="1000000000" step="any" value={duration} onChange={event => setDuration(event.target.valueAsNumber)} /></label>
      <label>Motion<select aria-label={name + ' timer mode'} value={mode} onChange={event => setMode(event.target.value as RampDriver['mode'])}><option value="once">Once</option><option value="repeat">Repeat</option><option value="pingpong">Bounce</option></select></label>
      <button className="quiet small" onClick={timer}>Apply timer</button></div>}
    {selected === 'source' && <div><label>Signal name<input aria-label={name + ' source'} value={source} maxLength={32} onChange={event => setSource(event.target.value)} /></label><button className="quiet small" disabled={!/^[A-Za-z0-9_.-]{1,32}$/.test(source)} onClick={() => selectSource(source)}>Bind source</button></div>}
    {mapping && <details><summary>{declaration.type === 'rgb8' ? 'Colour palette' : 'Response range'}</summary><div className="live-input-actions">
      <label>Source low<input aria-label={name + ' source minimum'} type="number" step="any" value={low} onChange={event => setLow(event.target.valueAsNumber)} /></label>
      <label>Source high<input aria-label={name + ' source maximum'} type="number" step="any" value={high} onChange={event => setHigh(event.target.valueAsNumber)} /></label>
      {declaration.type === 'rgb8' ? <><label>From colour<input type="color" aria-label={name + ' palette from'} value={'#' + from.toString(16).padStart(6, '0')} onChange={event => setFrom(parseInt(event.target.value.slice(1), 16))} /></label><label>To colour<input type="color" aria-label={name + ' palette to'} value={'#' + to.toString(16).padStart(6, '0')} onChange={event => setTo(parseInt(event.target.value.slice(1), 16))} /></label></> :
        <><label>Output from<input aria-label={name + ' output minimum'} type="number" step="any" value={from} onChange={event => setFrom(event.target.valueAsNumber)} /></label><label>Output to<input aria-label={name + ' output maximum'} type="number" step="any" value={to} onChange={event => setTo(event.target.valueAsNumber)} /></label></>}
      <button className="quiet small" disabled={!validRange} onClick={() => publish({ source: effective.source, timeout_ms: effective.timeout_ms, fallback: effective.fallback,
        ...(declaration.type === 'rgb8' ? { palette: { input_min: low, input_max: high, from, to } } : { map: { input_min: low, input_max: high, output_min: from, output_max: to } }) })}>Apply response</button>
    </div></details>}
    {isSource && <details><summary>When the source stops</summary><label>After (milliseconds)<input aria-label={name + ' timeout'} type="number" min="0" max="4294967295" step="1" value={effective.timeout_ms ?? 1000} onChange={event => { const timeout_ms = event.target.valueAsNumber; if (Number.isInteger(timeout_ms) && timeout_ms >= 0 && timeout_ms <= 0xffffffff) publish({ ...effective, timeout_ms }); }} /></label>
      <label>Use<select aria-label={name + ' fallback'} value={effective.fallback ?? 'default'} onChange={event => publish({ ...effective, fallback: event.target.value as 'default' | 'hold' })}><option value="default">Animation default</option><option value="hold">Last value</option></select></label></details>}
    {fallback && <p className="helper">Source unavailable; using the animation default.</p>}
  </div>;
}
