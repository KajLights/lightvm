import { VERSION } from './release';
import { useEffect, useState } from 'react';
import { Link, matchPath, Navigate, useLocation, useNavigate } from 'react-router-dom';
import { activityActions, outputFor, useAppDispatch, useAppSelector } from './store';
import { useRuntime } from './runtime';
import type { Path } from './types';
import { Develop } from './Develop';
import { Configure } from './Configure';
import { Firmware } from './Firmware';
import { Fader } from './Fader';

export function deviceRoute(id: string, view: string) { return '/devices/' + encodeURIComponent(id) + '/' + view; }
export function App() {
  const location = useLocation(), navigate = useNavigate();
  const device = useAppSelector(state => state.device), editor = useAppSelector(state => state.editor);
  const activity = useAppSelector(state => state.activity), dispatch = useAppDispatch();
  const { devices } = useRuntime();
  const route = matchPath('/devices/:deviceId/:view', location.pathname);
  const offline = location.pathname === '/develop';
  const validDevice = route?.params.deviceId === device.description?.device_id;
  const develop = offline || (validDevice && route?.params.view === 'develop');
  const configure = validDevice && route?.params.view === 'configure';
  const fader = validDevice && route?.params.view === 'fader';
  const firmware = location.pathname === '/firmware' || (validDevice && route?.params.view === 'firmware');
  const [openedDevelop, setOpenedDevelop] = useState(false), [openedConfigure, setOpenedConfigure] = useState(false);
  useEffect(() => { if (develop) setOpenedDevelop(true); if (configure) setOpenedConfigure(true); }, [develop, configure]);
  const workspace = Boolean(validDevice && route);
  const busy = Boolean(activity.operation), description = device.description;

  if (location.pathname === '/') return <Navigate to="/devices" replace />;
  const knownRoute = location.pathname === '/devices' || offline || firmware || (workspace && ['develop', 'fader', 'configure'].includes(route!.params.view || ''));
  return <>
    <header className="site-header">
      <Link className="brand" to="/devices"><span className="brand-symbol" aria-hidden="true">✳</span><span>LightVM</span><span className="version">{VERSION}</span></Link>
      <nav className="global-nav" aria-label="Application"><Link to="/devices" aria-current={location.pathname === '/devices' ? 'page' : undefined}>Devices</Link><Link to="/firmware" aria-current={location.pathname === '/firmware' ? 'page' : undefined}>Firmware</Link></nav>
      <span className={'badge ' + (device.selectedPath ? 'ready' : '')}><span className="status-dot" /><span>{device.selectedPath && description ? description.board : 'No device connected'}</span></span>
    </header>
    <main>
      {activity.notice && <div id="notice" className="notice" role="alert"><span>{activity.notice}</span><button className="quiet" aria-label="Dismiss notice" onClick={() => dispatch(activityActions.notice(''))}>×</button></div>}
      {workspace && description && <>
        <section className="device-bar" aria-label="Selected device">
          <div><p className="eyebrow">DEVICE WORKSPACE</p><strong id="board">{description.board}</strong><span className="device-id">{description.device_id} · LightVM {description.version}</span><p id="output-binding" className="helper">{device.config?.devices.find(item => item.id === device.selectedOutput) ? device.config.devices.find(item => item.id === device.selectedOutput)!.config.count + ' pixels · ' + device.selectedOutput : 'Output is not configured'}</p></div>
          <div className="connection-tools"><label className="sr-only" htmlFor="command-path">Command path</label>
            <select id="command-path" value={device.selectedPath || ''} disabled={busy || !device.paths.length} onChange={event => void devices.select(event.target.value as Path)}>
              {!device.selectedPath && <option value="">Choose a connection</option>}
              {device.paths.map(path => <option key={path.key} value={path.key}>{path.label}</option>)}
            </select>
            <button id="disconnect" className="quiet" disabled={busy || !device.selectedPath} onClick={() => void devices.disconnect()}>Disconnect path</button>
            <Link to="/devices">Connections &amp; devices</Link>
            <p id="path-health" className="helper">{(['usb', 'http'] as Path[]).map(key => key.toUpperCase() + ': ' + (device.paths.some(path => path.key === key) ? 'connected' : 'disconnected')).join(' · ')}</p>
          </div>
        </section>
        <nav className="workspace-tabs" aria-label="Device workspace">
          <Link to={deviceRoute(description.device_id, 'develop')} aria-current={develop ? 'page' : undefined}>Develop</Link>
          <Link to={deviceRoute(description.device_id, 'fader')} aria-current={fader ? 'page' : undefined}>Fader</Link>
          <Link to={deviceRoute(description.device_id, 'configure')} aria-current={configure ? 'page' : undefined}>Configure</Link>
          <Link to={deviceRoute(description.device_id, 'firmware')} aria-current={firmware ? 'page' : undefined}>Firmware</Link>
        </nav>
        {!device.selectedPath && <p className="connection-banner" role="status">{device.paths.length ? 'Select a remaining connection to continue. Commands stay on the path you choose.' : 'Device disconnected. Your drafts are still here.'} <Link to="/devices">Reconnect</Link></p>}
      </>}
      {location.pathname === '/devices' && <Devices onConnected={() => {
        const state = devices.snapshot;
        if (state.device.description) navigate(deviceRoute(state.device.description.device_id, outputFor(state.device.config) ? 'develop' : 'configure'));
      }} />}
      {openedDevelop && <div hidden={!develop}><Develop active={develop} /></div>}
      {openedConfigure && <div hidden={!configure}><Configure key={description?.device_id || 'none'} /></div>}
      {fader && <Fader />}
      {firmware && <Firmware usbConnected={device.paths.some(path => path.key === 'usb')} />}
      {!knownRoute && <section className="empty-state"><h1>Select a device to continue.</h1><p>Connect to a device to open its workspace.</p><Link className="button primary" to="/devices">Choose a device</Link></section>}
      {develop && <span id="compiler-state" className="sr-only">{editor.compiler === 'ready' ? 'Compiler ready' : editor.compiler === 'error' ? 'Compiler unavailable' : 'Loading compiler'}</span>}
      <details className="activity-panel"><summary>Activity <span>{activity.operation || (activity.entries.length ? activity.entries[0].text : 'No activity')}</span></summary><button id="clear-log" className="quiet small" onClick={() => dispatch(activityActions.clear())}>Clear</button><ol id="activity" aria-live="polite">{activity.entries.map(entry => <li key={entry.id} className={entry.kind}><span className="log-dot" /><span>{entry.text}</span></li>)}</ol></details>
      <footer className="page-footer"><span>LightVM · Device workspace</span><span>Made for a little code and a lot of light.</span></footer>
    </main>
  </>;
}

function Devices({ onConnected }: { onConnected: () => void }) {
  const state = useAppSelector(state => state.device), busy = useAppSelector(state => Boolean(state.activity.operation));
  const { devices } = useRuntime();
  const [address, setAddress] = useState(''), [selected, setSelected] = useState('');
  const hasHttp = state.paths.some(path => path.key === 'http'), hasUsb = state.paths.some(path => path.key === 'usb');
  const availableSerial = isSecureContext && Boolean(navigator.serial);
  useEffect(() => {
    if (address) return;
    const suggested = devices.suggestedAddress() || state.nearby[0]?.url;
    if (suggested) { setAddress(suggested); setSelected(state.nearby.find(item => item.url === suggested)?.device_id || ''); }
  }, [address, state.nearby, state.networkStatus, devices]);
  async function connect(path: Path) { if (await devices.connect(path, address)) onConnected(); }
  return <>
    <div className="intro"><div><p className="eyebrow">START WITH YOUR DEVICE</p><h1>Find your lights.</h1><p>Connect once, then move between programming and configuration.</p></div><Link className="quiet-link" to="/develop">Open the editor without a device →</Link></div>
    {state.paths.length > 0 && <section className="connected-card"><div><span className="status-dot connected" /> <strong>{state.description?.board}</strong><p className="helper">Your device is connected. You can add another path to the same device.</p></div><Link className="button secondary" to={deviceRoute(state.description!.device_id, 'develop')}>Open workspace</Link><button className="quiet" disabled={busy} onClick={() => void devices.disconnectAll()}>Disconnect device</button></section>}
    <div className="device-picker-grid">
      <section className="card device-card"><div className="card-heading"><h2>On your network</h2><button id="discover-devices" className="quiet small" disabled={busy || state.discovering} onClick={() => void devices.find([address])}>Refresh</button></div>
        <p className="muted">Choose a LightVM device on the same local network.</p>
        <label htmlFor="nearby-devices">Nearby devices</label><select id="nearby-devices" value={selected} disabled={busy || !state.nearby.length || hasHttp} onChange={event => { setSelected(event.target.value); const device = state.nearby.find(item => item.device_id === event.target.value); if (device) setAddress(device.url); }}><option value="">Choose a device</option>{state.nearby.map(device => <option key={device.device_id} value={device.device_id}>{device.board + ' · ' + device.hostname}</option>)}</select>
        <p id="discovery-state" className="helper" role="status">{state.discoveryMessage}</p>
        <label htmlFor="http-address">Device HTTP address</label><input id="http-address" type="url" value={address} onChange={event => { setAddress(event.target.value); setSelected(''); }} placeholder="http://lightvm.local" autoComplete="off" />
        <button id="connect-http" className="primary full" disabled={busy || hasHttp || !address} onClick={() => void connect('http')}>Connect HTTP <span aria-hidden="true">→</span></button>
        <p className="helper">Browser local-network permission may be required. A direct address also works.</p>
      </section>
      <section className="card device-card"><div className="card-heading"><h2>Plugged in over USB</h2></div><p className="muted">Connect a Pico or ESP32 running LightVM. The browser will ask you to choose its USB port.</p>
        <button id="connect" className="primary full" disabled={busy || hasUsb || !availableSerial} onClick={() => void connect('usb')}>Choose USB device <span aria-hidden="true">↗</span></button>
        <p className="helper">{!isSecureContext ? 'USB requires HTTPS or localhost.' : !navigator.serial ? 'USB requires a browser with Web Serial, such as desktop Chrome or Edge.' : 'Close other serial clients before connecting. Disconnecting leaves playback running.'}</p>
        <div className="setup-entry"><h3>Starting with a new board?</h3><p className="muted">Install LightVM firmware on a supported XIAO ESP32 board.</p><Link to="/firmware">Firmware installation →</Link></div>
      </section>
    </div>
  </>;
}
