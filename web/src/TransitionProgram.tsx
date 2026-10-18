import { useEffect, useRef, useState } from 'react';
import { crc32 } from '../protocol.mjs';
import { programManifest, settingsFor, type SettingsValues } from './library';
import { useRuntime } from './runtime';
import { useAppSelector } from './store';
import { SettingFields } from './SettingFields';
import type { Manifest, TransitionState } from './types';

export function TransitionProgram({ state, busy, onApply, id = 'transition-program' }: {
  state: TransitionState | null; busy: boolean;
  id?: string;
  onApply: (program: string, settings: SettingsValues) => Promise<void>;
}) {
  const { controllers } = useRuntime(), client = controllers.client;
  const controller = useAppSelector(snapshot => snapshot.controller);
  const [program, setProgram] = useState(state?.program ?? '');
  const [settings, setSettings] = useState<SettingsValues>({});
  const [metadata, setMetadata] = useState<{ key: string; manifest?: Manifest; error?: string } | null>(null);
  const [retry, setRetry] = useState(0);
  const observed = useRef<string | null>(null);
  const programs = controller.programs.filter(program => (program.type ?? program.profile) === 'transition');
  const selected = programs.find(item => item.name === program);
  const key = selected ? JSON.stringify([selected.name, selected.size, selected.crc32]) : '';
  const current = state ? JSON.stringify([state.program ?? '', state.program_crc32]) : null;

  useEffect(() => {
    if (current === null || current === observed.current) return;
    observed.current = current;
    if (program !== (state?.program ?? '')) { setProgram(state?.program ?? ''); setSettings({}); }
  }, [current, state?.program]);

  useEffect(() => {
    let active = true;
    setMetadata(null); setSettings({});
    if (client && selected) {
      const program = selected;
      void (async () => {
        try {
          const bytes = await client.getProgram(program.name, 'transition');
          if (bytes.length !== program.size || crc32(bytes) !== program.crc32) throw new Error('The stored transition changed. Refresh the controller catalogue and try again.');
          const manifest = programManifest(bytes);
          if (manifest.profile !== 'transition') throw new Error('Choose a stored transition program.');
          if (active) setMetadata({ key, manifest });
        } catch (error) {
          if (active) setMetadata({ key, error: (error as Error).message });
        }
      })();
    }
    return () => { active = false; };
  }, [client, key, retry]);

  const manifest = metadata?.key === key ? metadata.manifest : undefined;
  const error = metadata?.key === key ? metadata.error : undefined;
  let validation = '';
  if (manifest) { try { settingsFor(manifest, settings); } catch (error) { validation = (error as Error).message; } }

  return <div className="transition-program">
    <label htmlFor={id}>Transition program</label>
    <select id={id} value={program} disabled={busy || !controller.description?.transition_programs || !programs.length}
      onChange={event => { setProgram(event.target.value); setSettings({}); }}>
      <option value="">Choose a deployed transition</option>
      {program && !selected && <option value={program} disabled>{program} ({state?.program === program ? 'currently running' : 'unavailable'})</option>}
      {programs.map(program => <option key={program.name}>{program.name}</option>)}
    </select>
    {selected && !metadata && <p className="helper" role="status">Loading transition settings…</p>}
    {manifest && <fieldset disabled={busy}><SettingFields manifest={manifest} values={settings} onChange={setSettings} prefix={id + '-setting'} /></fieldset>}
    {manifest && manifest.settings.length > 0 && <p className="helper">Unchecked settings use the program defaults. Apply to use your changes.</p>}
    {(error || validation) && <p className="helper" role="alert">{error || validation}</p>}
    {error && <button className="quiet" disabled={busy} onClick={() => setRetry(value => value + 1)}>Retry settings</button>}
    <button className="secondary full" disabled={busy || !selected || !manifest || Boolean(validation) || !controller.description?.transition_programs}
      onClick={() => { if (manifest) void onApply(program, settingsFor(manifest, settings)); }}>Apply transition</button>
    {!programs.length && <p className="helper">Save a transition to the controller in Develop to choose it here.</p>}
    {state && <p className="helper">Active transition: {state.program || state.effect}</p>}
  </div>;
}
