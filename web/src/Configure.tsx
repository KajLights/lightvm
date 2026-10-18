import { useEffect, useMemo, useState } from 'react';
import { activityActions, useAppDispatch, useAppSelector } from './store';
import { useRuntime } from './runtime';
import type { HardwareConfig, NetworkConfig } from './types';
import { EMPTY_CONFIG } from './types';

function object(value: unknown): value is Record<string, unknown> {
  return Boolean(value && typeof value === 'object' && !Array.isArray(value));
}
function parseArguments(value: string): Record<string, number | boolean> | null {
  try {
    const args = JSON.parse(value);
    return object(args) && Object.values(args).every(item => typeof item === 'boolean' || (typeof item === 'number' && Number.isFinite(item))) ? args as Record<string, number | boolean> : null;
  } catch { return null; }
}
function parseConfig(value: string): HardwareConfig | null {
  try {
    const result = JSON.parse(value);
    if (!object(result) || result.schema !== 'lightvm.config' || !Array.isArray(result.devices) || Object.keys(result).some(key => key !== 'schema' && key !== 'devices')) return null;
    return result.devices.every(item => object(item) && typeof item.id === 'string' && item.type === 'ws2812b' && object(item.config) && typeof item.config.pin === 'string' && typeof item.config.count === 'number' &&
      (item.startup === undefined || (object(item.startup) && typeof item.startup.procedure === 'string' && (item.startup.args === undefined || object(item.startup.args))))) ? result as unknown as HardwareConfig : null;
  } catch { return null; }
}
function Hardware() {
  const { devices } = useRuntime(), device = useAppSelector(state => state.device), busy = useAppSelector(state => Boolean(state.activity.operation)), dispatch = useAppDispatch();
  const [text, setText] = useState(() => JSON.stringify(device.config || EMPTY_CONFIG, null, 2));
  const [base, setBase] = useState(() => ({ active_revision: device.configRevision, saved_revision: device.savedRevision, boot_id: device.description?.boot_id }));
  const [dirty, setDirty] = useState(false);
  // Keep incomplete argument JSON visible while the configuration retains its last valid value.
  const [argumentDrafts, setArgumentDrafts] = useState<Record<number, string>>({});
  useEffect(() => {
    if (!device.config || dirty) return;
    setText(JSON.stringify(device.config, null, 2)); setArgumentDrafts({});
    setBase({ active_revision: device.configRevision, saved_revision: device.savedRevision, boot_id: device.description?.boot_id });
  }, [device.config, device.configRevision, device.savedRevision, device.description?.boot_id, dirty]);
  const draft = useMemo(() => parseConfig(text), [text]);
  const invalidArguments = Object.values(argumentDrafts).some(value => !parseArguments(value));
  const stale = dirty && (base.active_revision !== device.configRevision || base.saved_revision !== device.savedRevision || base.boot_id !== device.description?.boot_id);
  const canApply = Boolean(device.selectedPath && draft && !invalidArguments && !busy && !stale);
  function update(config: HardwareConfig) { setText(JSON.stringify(config, null, 2)); setDirty(true); }
  function setOutput(index: number, change: (item: HardwareConfig['devices'][number]) => void) {
    if (!draft) return;
    const next = structuredClone(draft);
    change(next.devices[index]); update(next);
  }
  function addOutput() {
    if (!draft) return;
    const next = structuredClone(draft);
    let id = 'lights', suffix = 2;
    while (next.devices.some(item => item.id === id)) id = 'lights' + suffix++;
    const pin = device.description?.pins.find(pin => !next.devices.some(item => item.config.pin === pin)) || '';
    next.devices.push({ id, type: 'ws2812b', config: { pin, count: Math.min(100, device.description?.pixel_capacity || 100), order: 'GRB', brightness_limit: 255 } });
    update(next);
  }
  function removeOutput(index: number) {
    if (!draft) return;
    const next = structuredClone(draft); next.devices.splice(index, 1);
    setArgumentDrafts(current => Object.fromEntries(Object.entries(current).filter(([key]) => Number(key) !== index).map(([key, value]) => [Number(key) > index ? Number(key) - 1 : Number(key), value])));
    update(next);
  }
  function selectStartup(index: number, value: string) {
    setArgumentDrafts(current => { const next = { ...current }; delete next[index]; return next; });
    setOutput(index, item => { if (value) item.startup = { procedure: value }; else delete item.startup; });
  }
  function editArguments(index: number, value: string) {
    setArgumentDrafts(current => ({ ...current, [index]: value })); setDirty(true);
    const args = parseArguments(value);
    if (args) setOutput(index, item => { if (item.startup) item.startup.args = args; });
  }
  function reload() {
    if (!device.config) return;
    setDirty(false); setText(JSON.stringify(device.config, null, 2)); setArgumentDrafts({});
    setBase({ active_revision: device.configRevision, saved_revision: device.savedRevision, boot_id: device.description?.boot_id });
    dispatch(activityActions.notice(''));
  }
  async function submit(options: { save?: boolean; validate?: boolean }) {
    if (!draft || invalidArguments) { dispatch(activityActions.notice('Enter valid configuration and startup arguments before continuing.')); return; }
    const success = await devices.configure(draft, base, options);
    if (success && !options.validate) setDirty(false);
  }
  return <section className="card config-card"><div className="card-heading"><div><p className="eyebrow">OUTPUT &amp; STARTUP</p><h2>Set up the lights</h2></div><button id="config-refresh" className="quiet small" disabled={!device.selectedPath || busy} onClick={() => void devices.run('Refreshing configuration', async client => devices.readConfig(devices.require(client)))}>Refresh</button></div>
    <p className="muted">Configure each output and choose the animation it runs on reboot.</p>
    {stale && <div className="stale-banner" role="alert">Device configuration changed while you were editing. Review the latest values before saving. <button className="quiet small" onClick={reload}>Load device values</button></div>}
    {!draft && <p role="alert">Enter valid lightvm.config JSON with schema and devices. Startup belongs inside each device.</p>}
    <fieldset id="config-fields" disabled={!device.selectedPath || busy}>
      {draft?.devices.map((output, index) => <fieldset className="output-config" aria-label={'Output ' + (index + 1)} key={index}>
        <legend>{output.id || 'Unnamed output'}</legend>
        <div className="form-grid">
          <label htmlFor={'config-device-id-' + index}>Output name<input id={'config-device-id-' + index} value={output.id} onChange={event => setOutput(index, item => { item.id = event.target.value; })} autoComplete="off" /></label>
          <label htmlFor={'config-pin-' + index}>Data pin<select id={'config-pin-' + index} value={output.config.pin} onChange={event => setOutput(index, item => { item.config.pin = event.target.value; })}>{!device.description?.pins.includes(output.config.pin) && <option value={output.config.pin}>{output.config.pin || 'Select pin'}</option>}{device.description?.pins.map(pin => <option key={pin} value={pin}>{pin}</option>)}</select></label>
          <label htmlFor={'config-count-' + index}>Pixel count<input id={'config-count-' + index} type="number" min="1" max={device.description?.pixel_capacity} value={output.config.count} onChange={event => setOutput(index, item => { item.config.count = Number(event.target.value); })} /></label>
          <label htmlFor={'config-order-' + index}>Colour order<select id={'config-order-' + index} value={output.config.order || 'GRB'} onChange={event => setOutput(index, item => { item.config.order = event.target.value; })}>{['GRB', 'RGB', 'BRG', 'RBG', 'GBR', 'BGR'].map(order => <option key={order}>{order}</option>)}</select></label>
          <label htmlFor={'config-brightness-' + index}>Brightness limit (0–255)<input id={'config-brightness-' + index} type="number" min="0" max="255" value={output.config.brightness_limit ?? 255} onChange={event => setOutput(index, item => { item.config.brightness_limit = Number(event.target.value); })} /></label>
        </div>
        <label htmlFor={'startup-procedure-' + index}>Startup program</label><select id={'startup-procedure-' + index} value={output.startup?.procedure || ''} disabled={!device.description?.storage_available} onChange={event => selectStartup(index, event.target.value)}><option value="">Stay idle on reboot</option>{output.startup && !device.procedures.some(item => (item.type ?? item.profile ?? 'animation') === 'animation' && item.name === output.startup?.procedure) && <option value={output.startup.procedure}>{output.startup.procedure + ' (unavailable)'}</option>}{device.procedures.filter(item => (item.type ?? item.profile ?? 'animation') === 'animation').map(procedure => <option key={procedure.name} value={procedure.name}>{procedure.name + ' · ' + procedure.size + ' bytes'}</option>)}</select>
        {output.startup && <><label htmlFor={'startup-args-' + index}>Startup arguments (JSON)</label><textarea id={'startup-args-' + index} className="config-code" spellCheck={false} value={argumentDrafts[index] ?? JSON.stringify(output.startup.args || {}, null, 2)} aria-invalid={argumentDrafts[index] !== undefined && !parseArguments(argumentDrafts[index])} onChange={event => editArguments(index, event.target.value)} />
          {argumentDrafts[index] !== undefined && !parseArguments(argumentDrafts[index]) && <p role="alert">Arguments must be a JSON object with numeric or boolean values.</p>}
          <p className="helper">Arguments are passed when the animation initializes. Omitted arguments use its defaults.</p></>}
        {device.description?.mqtt_supported && <>
          <label htmlFor={'ha-preset-' + index}>Home Assistant ON preset</label>
          <select id={'ha-preset-' + index} value={output.home_assistant?.on_preset || ''} onChange={event => setOutput(index, item => { if (event.target.value) item.home_assistant = { ...item.home_assistant, on_preset: event.target.value }; else delete item.home_assistant; })}>
            <option value="">Do not expose this output</option>
            {output.home_assistant && !device.presets.some(item => item.name === output.home_assistant?.on_preset) && <option value={output.home_assistant.on_preset}>{output.home_assistant.on_preset + ' (unavailable)'}</option>}
            {device.presets.map(preset => <option key={preset.name} value={preset.name}>{preset.name}</option>)}
          </select>
          {output.home_assistant && <><label htmlFor={'ha-name-' + index}>Home Assistant name</label><input id={'ha-name-' + index} value={output.home_assistant.name || ''} placeholder={output.id} onChange={event => setOutput(index, item => { if (item.home_assistant) { if (event.target.value) item.home_assistant.name = event.target.value; else delete item.home_assistant.name; } })} /></>}
          <p className="helper">Each exposed output is one on/off light. ON starts its preset; OFF clears the LEDs. Enable MQTT in network settings.</p>
        </>}
        <button className="quiet small" onClick={() => removeOutput(index)}>Remove output</button>
      </fieldset>)}
      <button id="output-add" className="quiet" disabled={!draft} onClick={addOutput}>Add WS2812B output</button>
      <div className="form-actions"><button id="config-validate" className="quiet" disabled={!canApply} onClick={() => void submit({ validate: true })}>Validate</button><button id="config-apply" className="secondary" disabled={!canApply} onClick={() => void submit({})}>Apply in RAM</button><button id="config-save" className="primary" disabled={!canApply || !device.description?.storage_available || device.description?.storage_writable === false} onClick={() => void submit({ save: true })}>Apply &amp; save for reboot</button></div>
    </fieldset>
    <p className="helper">Unchanged hardware keeps playing when configuration is applied. Startup changes take effect after saving and rebooting.</p>
    <details className="advanced-config"><summary>Advanced JSON configuration</summary><label htmlFor="config-json">Device configuration JSON</label><textarea id="config-json" className="config-code" spellCheck={false} value={text} onChange={event => { setText(event.target.value); setArgumentDrafts({}); setDirty(true); }} /><p className="helper">The form and JSON editor update the same draft. Procedure arguments are validated on the device.</p></details>
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
  const [apPassword, setApPassword] = useState('');
  const [mqttPassword, setMqttPassword] = useState('');
  const [mqttClearPassword, setMqttClearPassword] = useState(false);
  const [apOpen, setApOpen] = useState(false);
  const [base, setBase] = useState(() => ({ revision: device.networkRevision, boot_id: device.description?.boot_id }));
  const [dirty, setDirty] = useState(false);
  const [nextId, setNextId] = useState(8);
  useEffect(() => {
    if (!device.networkConfig || dirty) return;
    setConfig(structuredClone(device.networkConfig)); setRows(rowsFrom(device.networkConfig));
    setApPassword(''); setApOpen(false); setMqttPassword(''); setMqttClearPassword(false);
    setBase({ revision: device.networkRevision, boot_id: device.description?.boot_id });
  }, [device.networkConfig, device.networkRevision, device.description?.boot_id, dirty]);
  if (!device.description?.wifi_supported && !device.description?.http_supported && !device.description?.mqtt_supported) return null;
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
    setApPassword(''); setApOpen(false); setMqttPassword(''); setMqttClearPassword(false);
    setBase({ revision: device.networkRevision, boot_id: device.description?.boot_id }); dispatch(activityActions.notice(''));
  }
  async function save() {
    if (!config) return;
    const next = structuredClone(config);
    next.wifi.networks = rows.map(row => ({ ssid: row.ssid, hidden: row.hidden, ...(row.open ? { password: '' } : row.password ? { password: row.password } : {}) }));
    if (device.description?.wifi_ap_supported) {
      next.wifi.ap = { ...next.wifi.ap, ...(apOpen ? { password: '' } : apPassword ? { password: apPassword } : {}) };
    }
    if (next.mqtt) {
      delete next.mqtt.password;
      if (mqttClearPassword) next.mqtt.password = '';
      else if (mqttPassword) next.mqtt.password = mqttPassword;
    }
    try {
      const success = await devices.configureNetwork(next, base.revision);
      if (success) setDirty(false);
    } finally { setRows(current => current.map(row => ({ ...row, password: '' }))); setApPassword(''); setMqttPassword(''); }
  }
  return <section className="card network-card"><div className="card-heading"><div><p className="eyebrow">CONNECTIVITY</p><h2>{device.description?.mqtt_supported ? 'Wi-Fi, HTTP & MQTT' : 'Wi-Fi & HTTP'}</h2></div><button id="network-refresh" className="quiet small" disabled={!device.selectedPath || busy} onClick={() => void devices.run('Refreshing network', async client => devices.readNetwork(devices.require(client)))}>Refresh</button></div>
    <p id="network-state" className="helper">{status ? [status.state, status.ssid, status.ip, status.error, status.retry_ms ? 'retry in ' + Math.ceil(status.retry_ms / 1000) + 's' : ''].filter(Boolean).join(' · ') : 'Connect to inspect network status.'}</p>
    {status?.initial_setup && <p className="helper">Initial setup access point. Save network settings to choose how this device connects after reboot.</p>}
    {status?.recovery && <p className="helper" role="status">Recovery access point. These are your saved network settings. Changes apply after restart; this AP stays available for the current boot.</p>}
    {stale && <div className="stale-banner" role="alert">Network settings changed while you were editing. <button className="quiet small" onClick={reload}>Load device values</button></div>}
    <fieldset id="network-fields" disabled={!device.selectedPath || busy || !device.description?.storage_available || device.description?.storage_writable === false || !config}>
      {device.description?.wifi_supported && <><label className="checkbox-label"><input id="wifi-enabled" type="checkbox" checked={Boolean(config?.wifi.enabled)} onChange={event => modify(draft => { draft.wifi.enabled = event.target.checked; })} /> Enable Wi-Fi</label>
        <label htmlFor="wifi-country">Radio country (XX = worldwide)</label><input id="wifi-country" value={config?.wifi.country || 'XX'} maxLength={2} autoComplete="off" onChange={event => modify(draft => { draft.wifi.country = event.target.value.toUpperCase(); })} />
        {device.description?.wifi_ap_supported && <><label htmlFor="wifi-mode">Wi-Fi mode</label><select id="wifi-mode" value={config?.wifi.mode || 'station'} onChange={event => modify(draft => { draft.wifi.mode = event.target.value as 'station' | 'ap'; })}><option value="station">Join Wi-Fi networks</option><option value="ap">Host an access point</option></select></>}
        {config?.wifi.mode === 'ap' && device.description?.wifi_ap_supported && <div className="wifi-network">
          <label htmlFor="ap-ssid">Access point SSID</label><input id="ap-ssid" value={config.wifi.ap?.ssid || ''} placeholder="Use device name (lightvm-xxxxxx)" autoComplete="off" spellCheck={false} onChange={event => modify(draft => { draft.wifi.ap = { ...draft.wifi.ap, ssid: event.target.value }; })} />
          <label htmlFor="ap-password">Access point password</label><input id="ap-password" type="password" value={apPassword} disabled={apOpen} placeholder="Keep saved password" autoComplete="off" data-1p-ignore="true" data-lpignore="true" data-bwignore="true" onChange={event => { setApPassword(event.target.value); setDirty(true); }} />
          <label className="checkbox-label"><input id="ap-open" type="checkbox" checked={apOpen} onChange={event => { setApOpen(event.target.checked); if (event.target.checked) setApPassword(''); setDirty(true); }} /> Use an open access point</label>
          <p className="helper">Leave the password empty to keep it. New access points are open by default. Connect at http://192.168.4.1 using the HTTP port below.</p>
        </div>}
        <div hidden={config?.wifi.mode === 'ap'}>
        <h3>Networks</h3><p className="muted">Networks are tried from top to bottom. Leave an existing password empty to retain it.</p>
        <div id="wifi-networks">{rows.map(row => <div className="wifi-network" key={row.id}>
          <label>SSID<input value={row.ssid} autoComplete="off" spellCheck={false} onChange={event => changeRow(row.id, { ssid: event.target.value })} /></label>
          <label>Password<input type="password" value={row.password} disabled={row.open} placeholder={row.ssid ? 'Keep saved password' : 'Enter password'} autoComplete="off" spellCheck={false} data-1p-ignore="true" data-lpignore="true" data-bwignore="true" onChange={event => changeRow(row.id, { password: event.target.value })} /></label>
          <label className="checkbox-label"><input type="checkbox" checked={row.hidden} onChange={event => changeRow(row.id, { hidden: event.target.checked })} /> Hidden SSID</label>
          <label className="checkbox-label"><input type="checkbox" checked={row.open} onChange={event => changeRow(row.id, { open: event.target.checked, password: event.target.checked ? '' : row.password })} /> Use an open network</label>
          <div className="button-pair"><button className="quiet" disabled={rows[0].id === row.id} onClick={() => move(row.id, -1)}>Move up</button><button className="quiet" disabled={rows.at(-1)?.id === row.id} onClick={() => move(row.id, 1)}>Move down</button><button className="quiet" onClick={() => { setRows(current => current.filter(item => item.id !== row.id)); setDirty(true); }}>Remove</button></div>
        </div>)}</div><button id="wifi-add" className="quiet" disabled={rows.length >= 8} onClick={() => { setRows(current => [...current, { id: nextId, ssid: '', password: '', hidden: false, open: false }]); setNextId(nextId + 1); setDirty(true); }}>Add network</button></div></>}
      {device.description?.http_supported && <><div className="divider" /><label className="checkbox-label"><input id="http-enabled" type="checkbox" checked={Boolean(config?.http.enabled)} onChange={event => modify(draft => { draft.http.enabled = event.target.checked; })} /> Enable HTTP commands</label><label htmlFor="http-port">HTTP port</label><input id="http-port" type="number" min={1} max={65535} value={config?.http.port || 80} onChange={event => modify(draft => { draft.http.port = Number(event.target.value); })} /></>}
      {device.description?.mqtt_supported && <><div className="divider" /><h3>MQTT</h3>
        <label className="checkbox-label"><input id="mqtt-enabled" type="checkbox" checked={Boolean(config?.mqtt?.enabled)} onChange={event => modify(draft => { draft.mqtt = { host: '', port: 1883, username: '', ...draft.mqtt, enabled: event.target.checked }; })} /> Enable MQTT control</label>
        {config?.mqtt && <>
          <label htmlFor="mqtt-host">Broker hostname or IPv4 address</label><input id="mqtt-host" value={config.mqtt.host} placeholder="mqtt.example.com" autoComplete="off" onChange={event => modify(draft => { if (draft.mqtt) draft.mqtt.host = event.target.value; })} />
          <label htmlFor="mqtt-port">MQTT port</label><input id="mqtt-port" type="number" min={1} max={65535} value={config.mqtt.port} onChange={event => modify(draft => { if (draft.mqtt) draft.mqtt.port = Number(event.target.value); })} />
          <label htmlFor="mqtt-user">MQTT username</label><input id="mqtt-user" value={config.mqtt.username} autoComplete="off" onChange={event => modify(draft => { if (draft.mqtt) draft.mqtt.username = event.target.value; })} />
          <label htmlFor="mqtt-password">MQTT password</label><input id="mqtt-password" type="password" value={mqttPassword} disabled={mqttClearPassword} placeholder="Keep saved password" autoComplete="off" data-1p-ignore="true" data-lpignore="true" data-bwignore="true" onChange={event => { setMqttPassword(event.target.value); setDirty(true); }} />
          <label className="checkbox-label"><input type="checkbox" checked={mqttClearPassword} onChange={event => { setMqttClearPassword(event.target.checked); setMqttPassword(''); setDirty(true); }} /> Clear MQTT password</label>
        </>}
        <p className="helper">Connect to the same broker as Home Assistant. This version uses plain MQTT on your trusted LAN. Choose each light's ON preset under Output &amp; startup.</p>
        <p role="status">{status?.mqtt_connected ? 'MQTT connected' : 'MQTT disconnected'}{status?.mqtt_error ? ' · ' + status.mqtt_error : ''}</p>
      </>}
      <button id="network-save" className="primary full" disabled={stale} onClick={() => void save()}>{status?.recovery ? 'Save network for next boot' : 'Save & reconnect network'}</button>
    </fieldset><p className="helper">Network changes keep the animation running. The HTTP connection may need reconnecting.</p>
  </section>;
}

export function Configure() {
  const device = useAppSelector(state => state.device), [section, setSection] = useState<'output' | 'network'>('output');
  const networkSupported = Boolean(device.description?.wifi_supported || device.description?.http_supported || device.description?.mqtt_supported);
  return <><div className="intro"><div><p className="eyebrow">CONFIGURE</p><h1>Make the device yours.</h1><p>Set its output, startup program and network access.</p></div></div>
    <nav className="subtabs" aria-label="Configuration sections"><button aria-current={section === 'output' ? 'page' : undefined} onClick={() => setSection('output')}>Output &amp; startup</button>{networkSupported && <button aria-current={section === 'network' ? 'page' : undefined} onClick={() => setSection('network')}>{device.description?.mqtt_supported ? 'Wi-Fi, HTTP & MQTT' : 'Wi-Fi & HTTP'}</button>}</nav>
    <div hidden={section !== 'output'}><Hardware /></div>
    {networkSupported && <div hidden={section !== 'network'}><Network /></div>}
  </>;
}
