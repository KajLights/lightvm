import { useEffect, useMemo, useState } from 'react';
import { activityActions, useAppDispatch, useAppSelector } from './store';
import { useRuntime } from './runtime';
import type { HardwareConfig, NetworkConfig } from './types';
import { EMPTY_CONFIG } from './types';

function parseConfig(value: string): HardwareConfig | null {
  try {
    const result = JSON.parse(value);
    return result?.schema === 'lightvm.config/1' && Array.isArray(result.devices) ? result : null;
  } catch { return null; }
}
function Hardware() {
  const { devices } = useRuntime(), device = useAppSelector(state => state.device), busy = useAppSelector(state => Boolean(state.activity.operation)), dispatch = useAppDispatch();
  const [text, setText] = useState(() => JSON.stringify(device.config || EMPTY_CONFIG, null, 2));
  const [base, setBase] = useState(() => ({ active_revision: device.configRevision, saved_revision: device.savedRevision, boot_id: device.description?.boot_id }));
  const [dirty, setDirty] = useState(false);
  const [startup, setStartup] = useState('');
  useEffect(() => {
    if (!device.config || dirty) return;
    setText(JSON.stringify(device.config, null, 2));
    setBase({ active_revision: device.configRevision, saved_revision: device.savedRevision, boot_id: device.description?.boot_id });
    setStartup(device.config.startup?.procedure || '');
  }, [device.config, device.configRevision, device.savedRevision, device.description?.boot_id, dirty]);
  const draft = useMemo(() => parseConfig(text), [text]), output = draft?.devices?.[0];
  const stale = dirty && (base.active_revision !== device.configRevision || base.saved_revision !== device.savedRevision || base.boot_id !== device.description?.boot_id);
  const canApply = Boolean(device.selectedPath && draft && !busy && !stale);
  function update(config: HardwareConfig) { setText(JSON.stringify(config, null, 2)); setDirty(true); }
  function setOutput(change: (item: HardwareConfig['devices'][number]) => void) {
    if (!draft) return;
    const next = structuredClone(draft);
    if (!next.devices.length) return;
    const previous = next.devices[0].id;
    change(next.devices[0]);
    if (next.targets?.A?.device === previous) next.targets.A.device = next.devices[0].id;
    update(next);
  }
  function toggleOutput(enabled: boolean) {
    if (!draft) return;
    const next = structuredClone(draft);
    if (enabled) {
      const id = 'lights';
      next.devices = [{ id, type: 'ws2812b', config: { pin: device.description?.pins[0] || '', count: Math.min(100, device.description?.pixel_capacity || 100), order: 'GRB', brightness_limit: 255 } }];
      next.targets = { A: { device: id } };
    } else { next.devices = []; next.targets = {}; next.startup = null; setStartup(''); }
    update(next);
  }
  function bind(enabled: boolean) {
    if (!draft || !output) return;
    const next = structuredClone(draft);
    next.targets = enabled ? { A: { device: output.id } } : {};
    if (!enabled) { next.startup = null; setStartup(''); }
    update(next);
  }
  function selectStartup(value: string) {
    setStartup(value);
    if (!draft) return;
    const next = structuredClone(draft);
    next.startup = value ? { procedure: value, target: 'A' } : null;
    update(next);
  }
  function reload() {
    if (!device.config) return;
    setDirty(false); setText(JSON.stringify(device.config, null, 2));
    setBase({ active_revision: device.configRevision, saved_revision: device.savedRevision, boot_id: device.description?.boot_id });
    setStartup(device.config.startup?.procedure || '');
    dispatch(activityActions.notice(''));
  }
  async function submit(options: { save?: boolean; validate?: boolean }) {
    if (!draft) { dispatch(activityActions.notice('Enter valid lightvm.config/1 JSON before continuing.')); return; }
    const success = await devices.configure(draft, base, options);
    if (success && !options.validate) setDirty(false);
  }
  return <section className="card config-card"><div className="card-heading"><div><p className="eyebrow">OUTPUT &amp; STARTUP</p><h2>Set up the lights</h2></div><button id="config-refresh" className="quiet small" disabled={!device.selectedPath || busy} onClick={() => void devices.run('Refreshing configuration', async client => devices.readConfig(devices.require(client)))}>Refresh</button></div>
    <p className="muted">Choose the pin and pixel count for this board. Bind the output to slot A to run animations.</p>
    {stale && <div className="stale-banner" role="alert">Device configuration changed while you were editing. Review the latest values before saving. <button className="quiet small" onClick={reload}>Load device values</button></div>}
    <fieldset id="config-fields" disabled={!device.selectedPath || busy}>
      <label className="checkbox-label"><input id="output-enabled" type="checkbox" checked={Boolean(output)} disabled={!draft} onChange={event => toggleOutput(event.target.checked)} /> Enable WS2812B output</label>
      {output && <div className="form-grid">
        <label htmlFor="config-device-id">Output name<input id="config-device-id" value={output.id} onChange={event => setOutput(item => { item.id = event.target.value; })} autoComplete="off" /></label>
        <label htmlFor="config-pin">Data pin<select id="config-pin" value={output.config.pin} onChange={event => setOutput(item => { item.config.pin = event.target.value; })}>{!device.description?.pins.includes(output.config.pin) && <option value={output.config.pin}>{output.config.pin || 'Select pin'}</option>}{device.description?.pins.map(pin => <option key={pin} value={pin}>{pin}</option>)}</select></label>
        <label htmlFor="config-count">Pixel count<input id="config-count" type="number" min="1" max={device.description?.pixel_capacity} value={output.config.count} onChange={event => setOutput(item => { item.config.count = Number(event.target.value); })} /></label>
        <label htmlFor="config-order">Colour order<select id="config-order" value={output.config.order || 'GRB'} onChange={event => setOutput(item => { item.config.order = event.target.value; })}>{['GRB', 'RGB', 'BRG', 'RBG', 'GBR', 'BGR'].map(order => <option key={order}>{order}</option>)}</select></label>
        <label htmlFor="config-brightness">Brightness limit (0–255)<input id="config-brightness" type="number" min="0" max="255" value={output.config.brightness_limit ?? 255} onChange={event => setOutput(item => { item.config.brightness_limit = Number(event.target.value); })} /></label>
        <label className="checkbox-label output-binding"><input id="config-bind" type="checkbox" checked={draft?.targets?.A?.device === output.id} onChange={event => bind(event.target.checked)} /> Bind this output to slot A</label>
      </div>}
      <div className="divider" /><h3>On startup</h3><p className="muted">Pick a saved program to run when this device reboots.</p>
      <label htmlFor="startup-procedure">Startup program</label><select id="startup-procedure" value={startup} disabled={!output || !draft?.targets?.A || !device.description?.storage_available} onChange={event => selectStartup(event.target.value)}><option value="">Stay idle on reboot</option>{device.procedures.map(procedure => <option key={procedure.name} value={procedure.name}>{procedure.name + ' · ' + procedure.size + ' bytes'}</option>)}</select>
      <div className="form-actions"><button id="config-validate" className="quiet" disabled={!canApply} onClick={() => void submit({ validate: true })}>Validate</button><button id="config-apply" className="secondary" disabled={!canApply} onClick={() => void submit({})}>Apply in RAM</button><button id="config-save" className="primary" disabled={!canApply || !device.description?.storage_available} onClick={() => void submit({ save: true })}>Apply &amp; save for reboot</button></div>
    </fieldset>
    <p className="helper">Applying output or startup configuration stops playback. Saving makes it available after reboot.</p>
    <details className="advanced-config"><summary>Advanced JSON configuration</summary><label htmlFor="config-json">Device configuration JSON</label><textarea id="config-json" className="config-code" spellCheck={false} value={text} onChange={event => { setText(event.target.value); setDirty(true); }} /><p className="helper">The form and JSON editor update the same draft. Validation happens on the device.</p></details>
  </section>;
}

type NetworkRow = { id: number; ssid: string; password: string; hidden: boolean; open: boolean };
function rowsFrom(config: NetworkConfig | null): NetworkRow[] {
  return (config?.wifi.networks || []).map((item, id) => ({ id, ssid: item.ssid, password: '', hidden: Boolean(item.hidden), open: false }));
}
function Network() {
  const { devices } = useRuntime(), device = useAppSelector(state => state.device), busy = useAppSelector(state => Boolean(state.activity.operation)), dispatch = useAppDispatch();
  const [config, setConfig] = useState<NetworkConfig | null>(() => device.networkConfig ? structuredClone(device.networkConfig) : null);
  const [rows, setRows] = useState<NetworkRow[]>(() => rowsFrom(device.networkConfig));
  const [base, setBase] = useState(() => ({ revision: device.networkRevision, boot_id: device.description?.boot_id }));
  const [dirty, setDirty] = useState(false);
  const [nextId, setNextId] = useState(8);
  useEffect(() => {
    if (!device.networkConfig || dirty) return;
    setConfig(structuredClone(device.networkConfig)); setRows(rowsFrom(device.networkConfig));
    setBase({ revision: device.networkRevision, boot_id: device.description?.boot_id });
  }, [device.networkConfig, device.networkRevision, device.description?.boot_id, dirty]);
  if (!device.description?.wifi_supported && !device.description?.http_supported) return null;
  const stale = dirty && (base.revision !== device.networkRevision || base.boot_id !== device.description?.boot_id);
  const status = device.networkStatus;
  function modify(patch: (draft: NetworkConfig) => void) { if (!config) return; const next = structuredClone(config); patch(next); setConfig(next); setDirty(true); }
  function changeRow(id: number, patch: Partial<NetworkRow>) { setRows(current => current.map(row => row.id === id ? { ...row, ...patch } : row)); setDirty(true); }
  function move(id: number, direction: number) {
    setRows(current => { const list = [...current], index = list.findIndex(row => row.id === id), next = index + direction; if (index >= 0 && next >= 0 && next < list.length) [list[index], list[next]] = [list[next], list[index]]; return list; }); setDirty(true);
  }
  function reload() {
    if (!device.networkConfig) return;
    setDirty(false); setConfig(structuredClone(device.networkConfig)); setRows(rowsFrom(device.networkConfig));
    setBase({ revision: device.networkRevision, boot_id: device.description?.boot_id }); dispatch(activityActions.notice(''));
  }
  async function save() {
    if (!config) return;
    const next = structuredClone(config);
    next.wifi.networks = rows.map(row => ({ ssid: row.ssid, hidden: row.hidden, ...(row.open ? { password: '' } : row.password ? { password: row.password } : {}) }));
    try {
      const success = await devices.configureNetwork(next, base.revision);
      if (success) setDirty(false);
    } finally { setRows(current => current.map(row => ({ ...row, password: '' }))); }
  }
  return <section className="card network-card"><div className="card-heading"><div><p className="eyebrow">CONNECTIVITY</p><h2>Wi-Fi &amp; HTTP</h2></div><button id="network-refresh" className="quiet small" disabled={!device.selectedPath || busy} onClick={() => void devices.run('Refreshing network', async client => devices.readNetwork(devices.require(client)))}>Refresh</button></div>
    <p id="network-state" className="helper">{status ? [status.state, status.ssid, status.ip, status.error, status.retry_ms ? 'retry in ' + Math.ceil(status.retry_ms / 1000) + 's' : ''].filter(Boolean).join(' · ') : 'Connect to inspect network status.'}</p>
    {stale && <div className="stale-banner" role="alert">Network settings changed while you were editing. <button className="quiet small" onClick={reload}>Load device values</button></div>}
    <fieldset id="network-fields" disabled={!device.selectedPath || busy || !device.description?.storage_available || !config}>
      {device.description?.wifi_supported && <><label className="checkbox-label"><input id="wifi-enabled" type="checkbox" checked={Boolean(config?.wifi.enabled)} onChange={event => modify(draft => { draft.wifi.enabled = event.target.checked; })} /> Enable Wi-Fi</label>
        <label htmlFor="wifi-country">Radio country (XX = worldwide)</label><input id="wifi-country" value={config?.wifi.country || 'XX'} maxLength={2} autoComplete="off" onChange={event => modify(draft => { draft.wifi.country = event.target.value.toUpperCase(); })} />
        <h3>Networks</h3><p className="muted">Networks are tried from top to bottom. Leave an existing password empty to retain it.</p>
        <div id="wifi-networks">{rows.map(row => <div className="wifi-network" key={row.id}>
          <label>SSID<input value={row.ssid} autoComplete="off" spellCheck={false} onChange={event => changeRow(row.id, { ssid: event.target.value })} /></label>
          <label>Password<input type="password" value={row.password} disabled={row.open} placeholder={row.ssid ? 'Keep saved password' : 'Enter password'} autoComplete="off" spellCheck={false} data-1p-ignore="true" data-lpignore="true" data-bwignore="true" onChange={event => changeRow(row.id, { password: event.target.value })} /></label>
          <label className="checkbox-label"><input type="checkbox" checked={row.hidden} onChange={event => changeRow(row.id, { hidden: event.target.checked })} /> Hidden SSID</label>
          <label className="checkbox-label"><input type="checkbox" checked={row.open} onChange={event => changeRow(row.id, { open: event.target.checked, password: event.target.checked ? '' : row.password })} /> Use an open network</label>
          <div className="button-pair"><button className="quiet" disabled={rows[0].id === row.id} onClick={() => move(row.id, -1)}>Move up</button><button className="quiet" disabled={rows.at(-1)?.id === row.id} onClick={() => move(row.id, 1)}>Move down</button><button className="quiet" onClick={() => { setRows(current => current.filter(item => item.id !== row.id)); setDirty(true); }}>Remove</button></div>
        </div>)}</div><button id="wifi-add" className="quiet" disabled={rows.length >= 8} onClick={() => { setRows(current => [...current, { id: nextId, ssid: '', password: '', hidden: false, open: false }]); setNextId(nextId + 1); setDirty(true); }}>Add network</button></>}
      {device.description?.http_supported && <><div className="divider" /><label className="checkbox-label"><input id="http-enabled" type="checkbox" checked={Boolean(config?.http.enabled)} onChange={event => modify(draft => { draft.http.enabled = event.target.checked; })} /> Enable HTTP commands</label><label htmlFor="http-port">HTTP port</label><input id="http-port" type="number" min={1} max={65535} value={config?.http.port || 80} onChange={event => modify(draft => { draft.http.port = Number(event.target.value); })} /></>}
      <button id="network-save" className="primary full" disabled={stale} onClick={() => void save()}>Save &amp; reconnect network</button>
    </fieldset><p className="helper">Network changes keep the animation running. The HTTP connection may need reconnecting.</p>
  </section>;
}

export function Configure() {
  const device = useAppSelector(state => state.device), [section, setSection] = useState<'output' | 'network'>('output');
  const networkSupported = Boolean(device.description?.wifi_supported || device.description?.http_supported);
  return <><div className="intro"><div><p className="eyebrow">CONFIGURE</p><h1>Make the device yours.</h1><p>Set its output, startup program and network access.</p></div></div>
    <nav className="subtabs" aria-label="Configuration sections"><button aria-current={section === 'output' ? 'page' : undefined} onClick={() => setSection('output')}>Output &amp; startup</button>{networkSupported && <button aria-current={section === 'network' ? 'page' : undefined} onClick={() => setSection('network')}>Wi-Fi &amp; HTTP</button>}</nav>
    <div hidden={section !== 'output'}><Hardware /></div>
    {networkSupported && <div hidden={section !== 'network'}><Network /></div>}
  </>;
}
