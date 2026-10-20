import { useEffect, useState } from 'react';
import { InputBindingControl, defaultBinding } from './InputBindingControl';
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
  return <details className="input-recipe"><summary>What controls this animation?</summary><p className="helper">Choose light controls, fixed values, timers or music. These bindings travel with the animation recipe.</p>
    {manifest.inputs?.map(input => {
      const binding = values[input.name] ?? defaultBinding(input);
      return <InputBindingControl key={input.name} declaration={input} binding={binding} value={'value' in binding ? binding.value : input.default} publish={next => {
        try { const updated = inputsFor(manifest, { ...values, [input.name]: next }); setError(''); onValidity?.(true); onChange(updated); }
        catch (problem) { setError((problem as Error).message); onValidity?.(false); }
      }} />;
    })}<details><summary>Advanced JSON</summary>
    <label htmlFor={prefix + '-inputs'}>Input bindings (JSON)</label><textarea id={prefix + '-inputs'} value={text} spellCheck={false} rows={5} onChange={event => {
      setText(event.target.value);
      try { const next = inputsFor(manifest, JSON.parse(event.target.value || '{}')); setError(''); onValidity?.(true); onChange(next); }
      catch (problem) { setError((problem as Error).message); onValidity?.(false); }
    }} /></details>{error && <p role="alert">{error}</p>}
  </details>;
}
