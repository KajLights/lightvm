import { useEffect, useState } from 'react';
import { inputsFor } from './program';
import type { InputBindings, Manifest } from './types';

export function InputRecipeFields({ manifest, values, onChange, onValidity, prefix }: {
  manifest: Manifest; values: InputBindings; onChange: (values: InputBindings) => void;
  onValidity?: (valid: boolean) => void; prefix: string;
}) {
  const encoded = JSON.stringify(values, null, 2);
  const [text, setText] = useState(encoded), [error, setError] = useState('');
  useEffect(() => { setText(encoded); setError(''); onValidity?.(true); }, [encoded]);
  if (!manifest.inputs?.length && !Object.keys(values).length && !error) return null;
  return <details className="input-recipe"><summary>Input recipe</summary><p className="helper">Leave empty for animation defaults, or supply values, timers and named sources. Live controls can copy their wiring here.</p>
    <label htmlFor={prefix + '-inputs'}>Input bindings (JSON)</label><textarea id={prefix + '-inputs'} value={text} spellCheck={false} rows={5} onChange={event => {
      setText(event.target.value);
      try { const next = inputsFor(manifest, JSON.parse(event.target.value || '{}')); setError(''); onValidity?.(true); onChange(next); }
      catch (problem) { setError((problem as Error).message); onValidity?.(false); }
    }} />{error && <p role="alert">{error}</p>}
  </details>;
}
