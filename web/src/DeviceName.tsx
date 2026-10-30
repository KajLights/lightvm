import { useEffect, useState } from 'react';
import { useAppSelector } from './store';
import { useRuntime } from './runtime';

export function DeviceName() {
  const controller = useAppSelector(state => state.controller);
  const busy = useAppSelector(state => Boolean(state.activity.operation));
  const { controllers } = useRuntime(), description = controller.description;
  const [name, setName] = useState(description?.nickname ?? ''), [dirty, setDirty] = useState(false);
  const [base, setBase] = useState(description);
  useEffect(() => { if (!dirty) { setName(description?.nickname ?? ''); setBase(description); } }, [description, dirty]);
  if (!description?.features?.includes('device_metadata')) return null;
  const stale = base?.controller_id !== description.controller_id || base?.boot_id !== description.boot_id || base?.metadata_revision !== description.metadata_revision;
  const invalid = name !== new TextDecoder().decode(new TextEncoder().encode(name)) || /[\u0000-\u001f\u007f]/u.test(name) || new TextEncoder().encode(name.replace(/^ +| +$/g, '')).length > 63;
  async function save() {
    const ok = await controllers.run('Saving device name', async connection => {
      const client = controllers.require(connection);
      await client.setDeviceNickname(name, base);
      await controllers.readConfig(client);
    });
    if (ok) setDirty(false);
  }
  return <section className="card"><h2>Device name</h2>
    <label htmlFor="device-nickname">Nickname</label>
    <input id="device-nickname" value={name} onChange={event => { setName(event.target.value); setDirty(true); }} disabled={busy} placeholder={description.board} />
    <p className="helper">Saved on the device and shared across browsers. Leave blank to clear it.</p>
    {description.metadata_error && <p role="alert">{description.metadata_error}</p>}
    {invalid && <p role="alert">Use up to 63 UTF-8 bytes without control characters.</p>}
    {stale && <p role="alert">Device details changed. Reload the name before saving.</p>}
    <button className="primary" onClick={() => void save()} disabled={!dirty || invalid || stale || busy || !controller.selectedPath || !description.storage_available || description.storage_writable === false}>Save name</button>
    <button className="quiet" disabled={busy} onClick={() => setDirty(false)}>Reload name</button>
  </section>;
}
