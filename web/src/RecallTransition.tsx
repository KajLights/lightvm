import { useEffect, useState } from 'react';
import { crc32 } from '../protocol.mjs';
import { programManifest, inputsFor } from './program';
import { SettingFields } from './SettingFields';
import { InputRecipeFields } from './InputRecipeFields';
import { useRuntime } from './runtime';
import { useAppSelector } from './store';
import type { Manifest, SceneSources, TransitionSpec } from './types';

// A preset owns one pinned transition invocation for recall, power and startup.
export function RecallTransition({ id, label, value, onChange, disabled = false, sceneSources, onValidity }: {
  id: string; label: string; value?: TransitionSpec; onChange: (value?: TransitionSpec) => void;
  disabled?: boolean; sceneSources?: SceneSources; onValidity?: (valid: boolean) => void;
}) {
  const { controllers } = useRuntime(), client = controllers.client;
  const programs = useAppSelector(state => state.controller.programs).filter(item => (item.type ?? item.profile) === 'transition');
  const selected = programs.find(item => item.name === value?.program);
  const [metadata, setMetadata] = useState<{ key: string; manifest?: Manifest; error?: string } | null>(null);
  const key = JSON.stringify([value?.program, value?.crc32, selected?.size, selected?.crc32]);
  useEffect(() => {
    let active = true; setMetadata(null); onValidity?.(!value);
    if (client && value && selected) {
      const name = value.program, expected = value.crc32, size = selected.size;
      void (async () => {
        try {
          const bytes = await client.getProgram(name, 'transition');
          if (crc32(bytes) !== expected || bytes.length !== size) throw new Error('This transition version is unavailable. Select the current version again.');
          const manifest = programManifest(bytes);
          if (manifest.profile !== 'transition') throw new Error('Choose a transition program.');
          inputsFor(manifest, value.inputs ?? {});
          if (active) { setMetadata({ key, manifest }); onValidity?.(true); }
        } catch (error) { if (active) setMetadata({ key, error: (error as Error).message }); }
      })();
    }
    return () => { active = false; };
  }, [client, key]);
  const manifest = metadata?.key === key ? metadata.manifest : undefined;
  const error = metadata?.key === key ? metadata.error : undefined;
  return <fieldset disabled={disabled} aria-label={label + ' settings'} className="recall-transition">
    <label htmlFor={id}>{label}</label>
    <select id={id} value={value?.program ?? ''} onChange={event => {
      const program = programs.find(item => item.name === event.target.value);
      onChange(program ? { program: program.name, crc32: program.crc32, duration_ms: value?.duration_ms ?? 2000, settings: {} } : undefined);
    }}>
      <option value="">Immediate cut</option>
      {value && !selected && <option value={value.program}>{value.program + ' (unavailable)'}</option>}
      {programs.map(item => <option key={item.name} value={item.name}>{item.name}</option>)}
    </select>
    {value && <>
      <label htmlFor={id + '-duration'}>{label + ' duration (ms)'}</label>
      <input id={id + '-duration'} type="number" min="0" step="any" value={value.duration_ms} onChange={event => onChange({ ...value, duration_ms: Number(event.target.value) })} />
      {manifest && <SettingFields manifest={manifest} values={value.settings ?? {}} prefix={id} onChange={settings => onChange({ ...value, settings })} />}
      {manifest && <InputRecipeFields manifest={manifest} values={value.inputs ?? {}} sceneSources={sceneSources} prefix={id + '-transition'} onChange={inputs => onChange({ ...value, inputs })} onValidity={onValidity} />}
      {error && <p role="alert">{error}</p>}
    </>}
  </fieldset>;
}
