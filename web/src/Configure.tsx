import { BoardStatus } from './BoardStatus';
import { Presets } from './Presets';
import { useEffect, useMemo, useState } from 'react';
import { activityActions, useAppDispatch, useAppSelector } from './store';
import { useRuntime } from './runtime';
import type { HardwareConfig, NetworkConfig } from './types';
import { EMPTY_CONFIG } from './types';

function object(value: unknown): value is Record<string, unknown> {
  return Boolean(value && typeof value === 'object' && !Array.isArray(value));
}
function parseConfig(value: string): HardwareConfig | null {
  try {
    const result = JSON.parse(value);
    if (!object(result) || result.schema !== 'lightvm.config' || !Array.isArray(result.outputs) || Object.keys(result).some(key => key !== 'schema' && key !== 'outputs' && key !== 'inputs')) return null;
    if (result.inputs !== undefined && (!Array.isArray(result.inputs) || !result.inputs.every(item => object(item) && typeof item.id === 'string' && item.type === 'tca9555' && object(item.config)))) return null;
    return result.outputs.every(item => object(item) && Object.keys(item).every(key => ['id', 'type', 'config', 'mode', 'startup', 'home_assistant'].includes(key)) && typeof item.id === 'string' && (item.type === 'ws2812b' || item.type === 'apa102') && object(item.config) && (item.type === 'ws2812b' ? typeof item.config.pin === 'string' : typeof item.config.data_pin === 'string' && typeof item.config.clock_pin === 'string') && typeof item.config.count === 'number' &&
      (item.home_assistant === undefined || (object(item.home_assistant) && Object.keys(item.home_assistant).every(key => ['default_preset', 'name'].includes(key)) && typeof item.home_assistant.default_preset === 'string')) &&
      (item.startup === undefined || (object(item.startup) && Object.keys(item.startup).every(key => key === 'mode' || key === 'preset') && (item.startup.mode === 'preset' ? typeof item.startup.preset === 'string' : (item.startup.mode === 'off' || item.startup.mode === 'restore_last') && item.startup.preset === undefined)))) ? result as unknown as HardwareConfig : null;
  } catch { return null; }
}
function Hardware() {
  const { controllers } = useRuntime(), controller = useAppSelector(state => state.controller), busy = useAppSelector(state => Boolean(state.activity.operation)), dispatch = useAppDispatch();
  const [text, setText] = useState(() => JSON.stringify(controller.config || EMPTY_CONFIG, null, 2));
  const [base, setBase] = useState(() => ({ active_revision: controller.configRevision, saved_revision: controller.savedRevision, boot_id: controller.description?.boot_id }));
  const [dirty, setDirty] = useState(false);
  useEffect(() => {
    if (!controller.config || dirty) return;
    setText(JSON.stringify(controller.config, null, 2));
    setBase({ active_revision: controller.configRevision, saved_revision: controller.savedRevision, boot_id: controller.description?.boot_id });
  }, [controller.config, controller.configRevision, controller.savedRevision, controller.description?.boot_id, dirty]);
  const draft = useMemo(() => parseConfig(text), [text]);
  const invalidStartup = draft?.outputs.some(item => item.startup?.mode === 'preset' && !/^[A-Za-z0-9_.-]{1,32}$/.test(item.startup.preset));
  const stale = dirty && (base.active_revision !== controller.configRevision || base.saved_revision !== controller.savedRevision || base.boot_id !== controller.description?.boot_id);
  const canApply = Boolean(controller.selectedPath && draft && !invalidStartup && !busy && !stale);
  function update(config: HardwareConfig) { setText(JSON.stringify(config, null, 2)); setDirty(true); }
  function setOutput(index: number, change: (item: HardwareConfig['outputs'][number]) => void) {
    if (!draft) return;
    const next = structuredClone(draft);
    change(next.outputs[index]); update(next);
  }
  function addOutput() {
    if (!draft) return;
    const next = structuredClone(draft);
    let id = 'lights', suffix = 2;
    while (next.outputs.some(item => item.id === id)) id = 'lights' + suffix++;
    const pin = controller.description?.pins.find(pin => !next.outputs.some(item => item.config.pin === pin)) || '';
    next.outputs.push({ id, type: 'ws2812b', config: { pin, count: Math.min(100, controller.description?.pixel_capacity || 100), order: 'GRB', brightness_limit: 255 } });
    update(next);
  }
  function addKeypad() {
    if (!draft) return;
    const next = structuredClone(draft);
    const id = 'keypad', output = 'key_lights';
    if (next.inputs?.some(item => item.id === id) || next.outputs.some(item => item.id === output)) return;
    next.outputs.push({ id: output, type: 'apa102', mode: 'indicators', config: { data_pin: 'GP19', clock_pin: 'GP18', enable_pin: 'GP17', count: 16, order: 'BGR', brightness_limit: 64 } });
    next.inputs = [...(next.inputs || []), { id, type: 'tca9555', config: { sda: 'GP4', scl: 'GP5', address: 32, active_low: true, debounce_ms: 20, long_press_ms: 500, multi_press_ms: 300 }, home_assistant: { indicators: output } }];
    update(next);
  }
  function removeOutput(index: number) {
    if (!draft) return;
    const next = structuredClone(draft), removed = next.outputs[index].id; next.outputs.splice(index, 1);
    next.inputs?.forEach(input => { if (input.home_assistant?.indicators === removed) delete input.home_assistant.indicators; });
    update(next);
  }
  function selectStartup(index: number, mode: 'off' | 'restore_last' | 'preset') {
    setOutput(index, item => {
      if (mode === 'preset') item.startup = { mode, preset: item.startup?.mode === 'preset' ? item.startup.preset : controller.presets[0]?.name || '' };
      else item.startup = { mode };
    });
  }
  function reload() {
    if (!controller.config) return;
    setDirty(false); setText(JSON.stringify(controller.config, null, 2));
    setBase({ active_revision: controller.configRevision, saved_revision: controller.savedRevision, boot_id: controller.description?.boot_id });
    dispatch(activityActions.notice(''));
  }
  async function submit(options: { save?: boolean; validate?: boolean }) {
    if (!draft || invalidStartup) { dispatch(activityActions.notice('Enter valid configuration before continuing.')); return; }
    const success = await controllers.configure(draft, base, options);
    if (success && !options.validate) setDirty(false);
  }
  return <section className="card config-card"><div className="card-heading"><div><p className="eyebrow">OUTPUT &amp; STARTUP</p><h2>Set up the lights</h2></div><button id="config-refresh" className="quiet small" disabled={!controller.selectedPath || busy} onClick={() => void controllers.run('Refreshing configuration', async client => controllers.readConfig(controllers.require(client)))}>Refresh</button></div>
    <p className="muted">Configure each output and choose its power-on behavior.</p>
    {stale && <div className="stale-banner" role="alert">Controller configuration changed while you were editing. Review the latest values before saving. <button className="quiet small" onClick={reload}>Load controller values</button></div>}
    {!draft && <p role="alert">Enter valid lightvm.config JSON with schema and outputs. Startup belongs inside each output.</p>}
    <fieldset id="config-fields" disabled={!controller.selectedPath || busy}>
      {draft?.outputs.map((output, index) => <fieldset className="output-config" aria-label={'Output ' + (index + 1)} key={index}>
        <legend>{output.id || 'Unnamed output'}</legend>
        <div className="form-grid">
          <label htmlFor={'config-controller-id-' + index}>Output name<input id={'config-controller-id-' + index} value={output.id} onChange={event => setOutput(index, item => { item.id = event.target.value; })} autoComplete="off" /></label>
          <label htmlFor={'config-pin-' + index}>Data pin<select id={'config-pin-' + index} value={output.config.pin ?? output.config.data_pin ?? ''} onChange={event => setOutput(index, item => { if (item.type === 'apa102') item.config.data_pin = event.target.value; else item.config.pin = event.target.value; })}>{!controller.description?.pins.includes(output.config.pin ?? output.config.data_pin ?? '') && <option value={output.config.pin ?? output.config.data_pin ?? ''}>{output.config.pin || output.config.data_pin || 'Select pin'}</option>}{controller.description?.pins.map(pin => <option key={pin} value={pin}>{pin}</option>)}</select></label>
          <label htmlFor={'config-count-' + index}>Pixel count<input id={'config-count-' + index} type="number" min="1" max={controller.description?.pixel_capacity} value={output.config.count} onChange={event => setOutput(index, item => { item.config.count = Number(event.target.value); })} /></label>
          <label htmlFor={'config-order-' + index}>Colour order<select id={'config-order-' + index} value={output.config.order || (output.type === 'apa102' ? 'BGR' : 'GRB')} onChange={event => setOutput(index, item => { item.config.order = event.target.value; })}>{['GRB', 'RGB', 'BRG', 'RBG', 'GBR', 'BGR'].map(order => <option key={order}>{order}</option>)}</select></label>
          <label htmlFor={'config-brightness-' + index}>Brightness limit (0–255)<input id={'config-brightness-' + index} type="number" min="0" max="255" value={output.config.brightness_limit ?? 255} onChange={event => setOutput(index, item => { item.config.brightness_limit = Number(event.target.value); })} /></label>
        </div>
        {output.type === 'apa102' && <p className="helper">APA102 clock: {output.config.clock_pin}; active-low enable: {output.config.enable_pin || 'none'}. Edit bus wiring in Advanced JSON.</p>}
        {output.mode === 'indicators' && <p className="helper">Home Assistant controls each RGB indicator independently. Configure its button input in Advanced JSON.</p>}
        {output.mode !== 'indicators' && <><label htmlFor={'startup-mode-' + index}>Power-on behavior</label>
        <select id={'startup-mode-' + index} value={output.startup?.mode || 'off'} disabled={!controller.description?.storage_available} onChange={event => selectStartup(index, event.target.value as 'off' | 'restore_last' | 'preset')}>
          <option value="off">Off</option><option value="restore_last" disabled={controller.description?.storage_writable === false}>Restore last state</option><option value="preset">Selected preset</option>
        </select>
        {output.startup?.mode === 'preset' && <><label htmlFor={'startup-preset-' + index}>Startup preset</label>
          <select id={'startup-preset-' + index} value={output.startup.preset} onChange={event => setOutput(index, item => { item.startup = { mode: 'preset', preset: event.target.value }; })}>
            <option value="">Select a saved preset</option>
            {output.startup.preset && !controller.presets.some(item => item.name === (output.startup?.mode === 'preset' ? output.startup.preset : '')) && <option value={output.startup.preset}>{output.startup.preset + ' (unavailable)'}</option>}
            {controller.presets.map(preset => <option key={preset.name} value={preset.name}>{preset.name}</option>)}
          </select>
        </>}
        <p className="helper">After a restart or power cut, stay off, restore the last selected preset and on/off state, or turn on with a saved preset. New outputs stay off. Temporary previews are not remembered.</p>
        <p className="helper">Each preset supplies its transition for selection, on/off and startup. Edit the transition under Presets.</p>
        {controller.description?.mqtt_supported && <>
          <label htmlFor={'ha-preset-' + index}>Home Assistant default preset</label>
          <select id={'ha-preset-' + index} value={output.home_assistant?.default_preset || ''} onChange={event => setOutput(index, item => { if (event.target.value) item.home_assistant = { ...item.home_assistant, default_preset: event.target.value }; else delete item.home_assistant; })}>
            <option value="">Do not expose this output</option>
            {output.home_assistant && !controller.presets.some(item => item.name === output.home_assistant?.default_preset) && <option value={output.home_assistant.default_preset}>{output.home_assistant.default_preset + ' (unavailable)'}</option>}
            {controller.presets.map(preset => <option key={preset.name} value={preset.name}>{preset.name}</option>)}
          </select>
          {output.home_assistant && <><p className="helper">All presets installed on this controller appear automatically in Home Assistant.</p><label htmlFor={'ha-name-' + index}>Home Assistant name</label><input id={'ha-name-' + index} value={output.home_assistant.name || ''} placeholder={output.id} onChange={event => setOutput(index, item => { if (item.home_assistant) { if (event.target.value) item.home_assistant.name = event.target.value; else delete item.home_assistant.name; } })} /></>}
          <p className="helper">Each exposed output has one light with power, brightness, colour and preset effects. Selecting an effect starts it; ON recalls the last selected preset, or the default. OFF transitions to black. Enable MQTT in network settings.</p>
        </>}
        </>}
        <button className="quiet small" onClick={() => removeOutput(index)}>Remove output</button>
      </fieldset>)}
      <button id="output-add" className="quiet" disabled={!draft} onClick={addOutput}>Add WS2812B output</button>
      {controller.description?.mqtt_supported && ['GP4', 'GP5', 'GP17', 'GP18', 'GP19'].every(pin => controller.description?.pins.includes(pin)) && <button className="quiet" disabled={!draft || draft.inputs?.some(item => item.id === 'keypad') || draft.outputs.some(item => item.id === 'key_lights')} onClick={addKeypad}>Add Pico RGB Keypad</button>}
      {draft?.inputs?.map(input => <p className="helper" key={input.id}>{input.id}: sixteen TCA9555 button inputs{input.home_assistant?.indicators ? ' with indicators on ' + input.home_assistant.indicators : ''}. Edit names and bindings in Advanced JSON.</p>)}
      <div className="form-actions"><button id="config-validate" className="quiet" disabled={!canApply} onClick={() => void submit({ validate: true })}>Validate</button><button id="config-apply" className="secondary" disabled={!canApply} onClick={() => void submit({})}>Apply in RAM</button><button id="config-save" className="primary" disabled={!canApply || !controller.description?.storage_available || controller.description?.storage_writable === false} onClick={() => void submit({ save: true })}>Apply &amp; save for reboot</button></div>
    </fieldset>
    <p className="helper">Unchanged hardware keeps playing when configuration is applied. Startup changes take effect after saving and rebooting.</p>
    <details className="advanced-config"><summary>Advanced JSON configuration</summary><label htmlFor="config-json">Controller configuration JSON</label><textarea id="config-json" className="config-code" spellCheck={false} value={text} onChange={event => { setText(event.target.value); setDirty(true); }} /><p className="helper">The form and JSON editor update the same draft. Program arguments are validated on the controller.</p></details>
  </section>;
}

type NetworkRow = { id: number; ssid: string; password: string; hidden: boolean; open: boolean };
function rowsFrom(config: NetworkConfig | null): NetworkRow[] {
  return (config?.wifi.networks || []).map((item, id) => ({ id, ssid: item.ssid, password: '', hidden: Boolean(item.hidden), open: false }));
}
function Network() {
  const { controllers } = useRuntime(), controller = useAppSelector(state => state.controller), busy = useAppSelector(state => Boolean(state.activity.operation)), dispatch = useAppDispatch();
  const [config, setConfig] = useState<NetworkConfig | null>(() => controller.networkConfig ? structuredClone(controller.networkConfig) : null);
  const [rows, setRows] = useState<NetworkRow[]>(() => rowsFrom(controller.networkConfig));
  const [apPassword, setApPassword] = useState('');
  const [mqttPassword, setMqttPassword] = useState('');
  const [mqttClearPassword, setMqttClearPassword] = useState(false);
  const [apOpen, setApOpen] = useState(false);
  const [base, setBase] = useState(() => ({ revision: controller.networkRevision, boot_id: controller.description?.boot_id }));
  const [dirty, setDirty] = useState(false);
  const [nextId, setNextId] = useState(8);
  useEffect(() => {
    if (!controller.networkConfig || dirty) return;
    setConfig(structuredClone(controller.networkConfig)); setRows(rowsFrom(controller.networkConfig));
    setApPassword(''); setApOpen(false); setMqttPassword(''); setMqttClearPassword(false);
    setBase({ revision: controller.networkRevision, boot_id: controller.description?.boot_id });
  }, [controller.networkConfig, controller.networkRevision, controller.description?.boot_id, dirty]);
  if (!controller.description?.wifi_supported && !controller.description?.http_supported && !controller.description?.mqtt_supported) return null;
  const stale = dirty && (base.revision !== controller.networkRevision || base.boot_id !== controller.description?.boot_id);
  const status = controller.networkStatus;
  function modify(patch: (draft: NetworkConfig) => void) { if (!config) return; const next = structuredClone(config); patch(next); setConfig(next); setDirty(true); }
  function changeRow(id: number, patch: Partial<NetworkRow>) { setRows(current => current.map(row => row.id === id ? { ...row, ...patch } : row)); setDirty(true); }
  function move(id: number, direction: number) {
    setRows(current => { const list = [...current], index = list.findIndex(row => row.id === id), next = index + direction; if (index >= 0 && next >= 0 && next < list.length) [list[index], list[next]] = [list[next], list[index]]; return list; }); setDirty(true);
  }
  function reload() {
    if (!controller.networkConfig) return;
    setDirty(false); setConfig(structuredClone(controller.networkConfig)); setRows(rowsFrom(controller.networkConfig));
    setApPassword(''); setApOpen(false); setMqttPassword(''); setMqttClearPassword(false);
    setBase({ revision: controller.networkRevision, boot_id: controller.description?.boot_id }); dispatch(activityActions.notice(''));
  }
  async function save() {
    if (!config) return;
    const next = structuredClone(config);
    next.wifi.networks = rows.map(row => ({ ssid: row.ssid, hidden: row.hidden, ...(row.open ? { password: '' } : row.password ? { password: row.password } : {}) }));
    if (controller.description?.wifi_ap_supported) {
      next.wifi.ap = { ...next.wifi.ap, ...(apOpen ? { password: '' } : apPassword ? { password: apPassword } : {}) };
    }
    if (next.mqtt) {
      delete next.mqtt.password;
      if (mqttClearPassword) next.mqtt.password = '';
      else if (mqttPassword) next.mqtt.password = mqttPassword;
    }
    try {
      const success = await controllers.configureNetwork(next, base.revision);
      if (success) setDirty(false);
    } finally { setRows(current => current.map(row => ({ ...row, password: '' }))); setApPassword(''); setMqttPassword(''); }
  }
  return <section className="card network-card"><div className="card-heading"><div><p className="eyebrow">CONNECTIVITY</p><h2>{controller.description?.mqtt_supported ? 'Wi-Fi, HTTP & MQTT' : 'Wi-Fi & HTTP'}</h2></div><button id="network-refresh" className="quiet small" disabled={!controller.selectedPath || busy} onClick={() => void controllers.run('Refreshing network', async client => controllers.readNetwork(controllers.require(client)))}>Refresh</button></div>
    <p id="network-state" className="helper">{status ? [status.state, status.ssid, status.ip, status.error, status.retry_ms ? 'retry in ' + Math.ceil(status.retry_ms / 1000) + 's' : ''].filter(Boolean).join(' · ') : 'Connect to inspect network status.'}</p>
    {status?.initial_setup && <p className="helper">Initial setup access point. Save network settings to choose how this controller connects after reboot.</p>}
    {status?.recovery && <p className="helper" role="status">Recovery access point. These are your saved network settings. Changes apply after restart; this AP stays available for the current boot.</p>}
    {stale && <div className="stale-banner" role="alert">Network settings changed while you were editing. <button className="quiet small" onClick={reload}>Load controller values</button></div>}
    <fieldset id="network-fields" disabled={!controller.selectedPath || busy || !controller.description?.storage_available || controller.description?.storage_writable === false || !config}>
      {controller.description?.wifi_supported && <><label className="checkbox-label"><input id="wifi-enabled" type="checkbox" checked={Boolean(config?.wifi.enabled)} onChange={event => modify(draft => { draft.wifi.enabled = event.target.checked; })} /> Enable Wi-Fi</label>
        <label htmlFor="wifi-country">Radio country (XX = worldwide)</label><input id="wifi-country" value={config?.wifi.country || 'XX'} maxLength={2} autoComplete="off" onChange={event => modify(draft => { draft.wifi.country = event.target.value.toUpperCase(); })} />
        {controller.description?.wifi_ap_supported && <><label htmlFor="wifi-mode">Wi-Fi mode</label><select id="wifi-mode" value={config?.wifi.mode || 'station'} onChange={event => modify(draft => { draft.wifi.mode = event.target.value as 'station' | 'ap'; })}><option value="station">Join Wi-Fi networks</option><option value="ap">Host an access point</option></select></>}
        {config?.wifi.mode === 'ap' && controller.description?.wifi_ap_supported && <div className="wifi-network">
          <label htmlFor="ap-ssid">Access point SSID</label><input id="ap-ssid" value={config.wifi.ap?.ssid || ''} placeholder="Use controller name (lightvm-xxxxxx)" autoComplete="off" spellCheck={false} onChange={event => modify(draft => { draft.wifi.ap = { ...draft.wifi.ap, ssid: event.target.value }; })} />
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
      {controller.description?.http_supported && <><div className="divider" /><label className="checkbox-label"><input id="http-enabled" type="checkbox" checked={Boolean(config?.http.enabled)} onChange={event => modify(draft => { draft.http.enabled = event.target.checked; })} /> Enable HTTP commands</label><label htmlFor="http-port">HTTP port</label><input id="http-port" type="number" min={1} max={65535} value={config?.http.port || 80} onChange={event => modify(draft => { draft.http.port = Number(event.target.value); })} /></>}
      {controller.description?.mqtt_supported && <><div className="divider" /><h3>MQTT</h3>
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
  const controller = useAppSelector(state => state.controller), [section, setSection] = useState<'presets' | 'output' | 'network' | 'status'>('presets');
  const networkSupported = Boolean(controller.description?.wifi_supported || controller.description?.http_supported || controller.description?.mqtt_supported);
  return <><div className="intro"><div><p className="eyebrow">02 / CONFIGURATION</p><h1>Make the controller yours.</h1><p>Build presets, configure outputs, and connect Wi-Fi and MQTT.</p></div></div>
    <nav className="subtabs" aria-label="Configuration sections"><button aria-current={section === 'presets' ? 'page' : undefined} onClick={() => setSection('presets')}>Presets</button><button aria-current={section === 'output' ? 'page' : undefined} onClick={() => setSection('output')}>Output &amp; startup</button>{networkSupported && <button aria-current={section === 'network' ? 'page' : undefined} onClick={() => setSection('network')}>{controller.description?.mqtt_supported ? 'Wi-Fi, HTTP & MQTT' : 'Wi-Fi & HTTP'}</button>}<button aria-current={section === 'status' ? 'page' : undefined} onClick={() => setSection('status')}>Board status</button></nav>
    <div hidden={section !== 'status'}><BoardStatus active={section === 'status'} /></div>
    <div hidden={section !== 'presets'}><Presets /></div>
    <div hidden={section !== 'output'}><Hardware /></div>
    {networkSupported && <div hidden={section !== 'network'}><Network /></div>}
  </>;
}
