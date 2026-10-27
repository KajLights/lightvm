import { useEffect, useState } from 'react';
import { useRuntime } from './runtime';
import { useAppSelector } from './store';
import { ANALYSIS_PROFILES, assembleSources, providerMatches, sourceEntry, type SourceEntry } from './scene-sources';
import type { AudioProviderStatus, SceneSource } from './types';

export function SceneSources({ entries, onChange }: { entries: SourceEntry[]; onChange: (entries: SourceEntry[]) => void }) {
  const { controllers } = useRuntime(), client = controllers.client;
  const supported = useAppSelector(state => state.controller.description?.audio_subscriptions_supported);
  const [providers, setProviders] = useState<AudioProviderStatus[]>([]), [statusError, setStatusError] = useState('');
  const { error } = assembleSources(entries);
  useEffect(() => {
    let alive = true, reading = false; setProviders([]); setStatusError('');
    async function read() {
      if (!client || !supported || reading || controllers.snapshot.activity.operation) return;
      reading = true;
      try { const status = await client.json('audio_status'); if (alive) { setProviders(status.providers); setStatusError(''); } }
      catch (error) { if (alive) setStatusError((error as Error).message); }
      finally { reading = false; }
    }
    void read(); const timer = setInterval(() => void read(), 1000);
    return () => { alive = false; clearInterval(timer); };
  }, [client, supported]);
  function patch(index: number, source: SceneSource) { onChange(entries.map((row, n) => n === index ? { ...row, source } : row)); }
  return <section className="scene-sources" aria-label="Preset sources"><h3>Sources</h3>
    <p className="helper">Declare a Sense or host service, or a browser gateway. Saving validates the recipe; activation starts its subscriptions. If you rename an alias, update its input bindings too.</p>
    {supported === false && <p role="status">This controller firmware does not support audio source subscriptions.</p>}
    {entries.map((row, index) => {
      const source = row.source, status = providers.find(provider => providerMatches(source, provider));
      const observed = providers.find(provider => providerMatches(source, provider, false) && /^[a-f\d]{16}$/i.test(provider.source_id) && !/^0{16}$/.test(provider.source_id));
      const label = 'Source ' + (index + 1);
      return <fieldset className="source-card" key={row.key} aria-label={label}>
        <legend>{row.alias || label}</legend>
        <div className="form-grid">
          <label>Alias<input aria-label={label + ' alias'} value={row.alias} maxLength={23} onChange={event => onChange(entries.map((r, n) => n === index ? { ...r, alias: event.target.value } : r))} /></label>
          <label>Provider<select aria-label={label + ' provider'} value={source.type} onChange={event => patch(index, event.target.value === 'lvaf' ? { type: 'lvaf', address: '' } : { type: 'gateway', name: 'music' })}><option value="lvaf">Sense / host (UDP)</option><option value="gateway">Browser gateway</option></select></label>
          {source.type === 'lvaf' ? <>
            <label>Address<input aria-label={label + ' address'} value={source.address} maxLength={127} placeholder="192.168.1.40 or router DNS" onChange={event => patch(index, { ...source, address: event.target.value })} /></label>
            <label>UDP port<input aria-label={label + ' port'} type="number" min={1024} max={65535} placeholder="49152" value={source.port ?? ''} onChange={event => patch(index, { ...source, port: event.target.value ? Number(event.target.value) : undefined })} /></label>
          </> : <label>Gateway name<input aria-label={label + ' gateway name'} value={source.name} maxLength={32} onChange={event => patch(index, { ...source, name: event.target.value })} /></label>}
          <label>Expected algorithm<select aria-label={label + ' profile'} value={source.analysis_profile ?? 1} onChange={event => patch(index, { ...source, analysis_profile: Number(event.target.value) as 1 | 2 })}>{ANALYSIS_PROFILES.map(profile => <option key={profile.id} value={profile.id}>{profile.label}</option>)}</select></label>
          <label>Identity pin (optional)<input aria-label={label + ' identity pin'} value={source.source_id ?? ''} maxLength={16} placeholder="16 hexadecimal digits" onChange={event => patch(index, { ...source, source_id: event.target.value || undefined })} /></label>
        </div>
        <label className="checkbox-label"><input aria-label={label + ' allow synthetic'} type="checkbox" checked={source.allow_synthetic ?? false} onChange={event => patch(index, { ...source, allow_synthetic: event.target.checked })} /> Allow synthetic fixture streams</label>
        <p className="helper">{source.source_id ? 'Pinned: a different source identity stays unavailable.' : 'Unpinned: accept the identity returned by this provider.'}</p>
        <p role="status">{status ? `${status.status} · profile ${status.profile} · ${status.bands} spectrum bands · ${status.references} consumers` : 'Inactive: no matching active subscription. Activate the scene to connect.'}</p>
        {status?.source_id && !/^0{16}$/.test(status.source_id) && <p className="helper">Source {status.source_id} · session {status.session} · {status.packets} packets · {status.age_rejections} age rejections{status.band_edges.length ? ' · band edges ' + status.band_edges.join(', ') : ''}</p>}
        <div className="button-pair">{observed && source.source_id?.toLowerCase() !== observed.source_id && <button className="quiet" onClick={() => patch(index, { ...source, source_id: observed.source_id })}>Pin observed identity {observed.source_id}</button>}
          <button className="quiet" aria-label={'Remove ' + label.toLowerCase()} onClick={() => onChange(entries.filter((_, n) => n !== index))}>Remove source</button></div>
      </fieldset>;
    })}
    <button className="secondary" disabled={entries.length >= 4 || supported === false} onClick={() => {
      let alias = 'audio', n = 2; while (entries.some(row => row.alias === alias)) alias = 'audio' + n++;
      onChange([...entries, sourceEntry(alias, { type: 'lvaf', address: '' })]);
    }}>+ Add source</button>
    {error && <p role="alert">{error}</p>}{statusError && <p role="status">Source status unavailable: {statusError}</p>}
  </section>;
}
