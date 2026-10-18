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

export function controllerRoute(id: string, view: string) { return '/controllers/' + encodeURIComponent(id) + '/' + view; }
export function App() {
  const location = useLocation(), navigate = useNavigate();
  const controller = useAppSelector(state => state.controller), editor = useAppSelector(state => state.editor);
  const activity = useAppSelector(state => state.activity), dispatch = useAppDispatch();
  const { controllers } = useRuntime();
  const route = matchPath('/controllers/:controllerId/:view', location.pathname);
  const offline = location.pathname === '/develop';
  const validController = route?.params.controllerId === controller.description?.controller_id;
  const develop = offline || (validController && route?.params.view === 'develop');
  const configure = validController && route?.params.view === 'configure';
  const fader = validController && route?.params.view === 'fader';
  const firmware = location.pathname === '/firmware' || (validController && route?.params.view === 'firmware');
  const [openedDevelop, setOpenedDevelop] = useState(false), [openedConfigure, setOpenedConfigure] = useState(false);
  useEffect(() => { if (develop) setOpenedDevelop(true); if (configure) setOpenedConfigure(true); }, [develop, configure]);
  const workspace = Boolean(validController && route);
  const busy = Boolean(activity.operation), description = controller.description;

  if (location.pathname === '/') return <Navigate to="/controllers" replace />;
  const knownRoute = location.pathname === '/controllers' || offline || firmware || (workspace && ['develop', 'fader', 'configure'].includes(route!.params.view || ''));
  return <>
    <header className="site-header">
      <Link className="brand" to="/controllers"><span className="brand-symbol" aria-hidden="true">✳</span><span>LightVM</span><span className="version">{VERSION}</span></Link>
      <nav className="global-nav" aria-label="Application"><Link to="/controllers" aria-current={location.pathname === '/controllers' ? 'page' : undefined}>Controllers</Link><Link to="/firmware" aria-current={location.pathname === '/firmware' ? 'page' : undefined}>Firmware</Link></nav>
      <span className={'badge ' + (controller.selectedPath ? 'ready' : '')}><span className="status-dot" /><span>{controller.selectedPath && description ? description.board : 'No controller connected'}</span></span>
    </header>
    <main>
      {activity.notice && <div id="notice" className="notice" role="alert"><span>{activity.notice}</span><button className="quiet" aria-label="Dismiss notice" onClick={() => dispatch(activityActions.notice(''))}>×</button></div>}
      {workspace && description && <>
        <section className="controller-bar" aria-label="Selected controller">
          <div><p className="eyebrow">CONTROLLER WORKSPACE</p><strong id="board">{description.board}</strong><span className="controller-id">{description.controller_id} · LightVM {description.version}</span><p id="output-binding" className="helper">{controller.config?.outputs.find(item => item.id === controller.selectedOutput) ? controller.config.outputs.find(item => item.id === controller.selectedOutput)!.config.count + ' pixels · ' + controller.selectedOutput : 'Output is not configured'}</p></div>
          <div className="connection-tools"><label className="sr-only" htmlFor="command-path">Command path</label>
            <select id="command-path" value={controller.selectedPath || ''} disabled={busy || !controller.paths.length} onChange={event => void controllers.select(event.target.value as Path)}>
              {!controller.selectedPath && <option value="">Choose a connection</option>}
              {controller.paths.map(path => <option key={path.key} value={path.key}>{path.label}</option>)}
            </select>
            <button id="disconnect" className="quiet" disabled={busy || !controller.selectedPath} onClick={() => void controllers.disconnect()}>Disconnect path</button>
            <Link to="/controllers">Connections &amp; controllers</Link>
            <p id="path-health" className="helper">{(['usb', 'http'] as Path[]).map(key => {
              const path = controller.paths.find(path => path.key === key);
              return (path?.protocol || (key === 'usb' ? 'USB' : 'Network')) + ': ' + (path ? 'connected' : 'disconnected');
            }).join(' · ')}</p>
          </div>
        </section>
        <nav className="workspace-tabs" aria-label="Controller workspace">
          <Link to={controllerRoute(description.controller_id, 'develop')} aria-current={develop ? 'page' : undefined}>Develop</Link>
          <Link to={controllerRoute(description.controller_id, 'fader')} aria-current={fader ? 'page' : undefined}>Fader</Link>
          <Link to={controllerRoute(description.controller_id, 'configure')} aria-current={configure ? 'page' : undefined}>Configure</Link>
          <Link to={controllerRoute(description.controller_id, 'firmware')} aria-current={firmware ? 'page' : undefined}>Firmware</Link>
        </nav>
        {!controller.selectedPath && <p className="connection-banner" role="status">{controller.paths.length ? 'Select a remaining connection to continue. Commands stay on the path you choose.' : 'Controller disconnected. Your drafts are still here.'} <Link to="/controllers">Reconnect</Link></p>}
      </>}
      {location.pathname === '/controllers' && <Controllers onConnected={() => {
        const state = controllers.snapshot;
        if (state.controller.description) navigate(controllerRoute(state.controller.description.controller_id, outputFor(state.controller.config) ? 'develop' : 'configure'));
      }} />}
      {openedDevelop && <div hidden={!develop}><Develop active={develop} /></div>}
      {openedConfigure && <div hidden={!configure}><Configure key={description?.controller_id || 'none'} /></div>}
      {fader && <Fader />}
      {firmware && <Firmware usbConnected={controller.paths.some(path => path.key === 'usb')} />}
      {!knownRoute && <section className="empty-state"><h1>Select a controller to continue.</h1><p>Connect to a controller to open its workspace.</p><Link className="button primary" to="/controllers">Choose a controller</Link></section>}
      {develop && <span id="compiler-state" className="sr-only">{editor.compiler === 'ready' ? 'Compiler ready' : editor.compiler === 'error' ? 'Compiler unavailable' : 'Loading compiler'}</span>}
      <details className="activity-panel"><summary>Activity <span>{activity.operation || (activity.entries.length ? activity.entries[0].text : 'No activity')}</span></summary><button id="clear-log" className="quiet small" onClick={() => dispatch(activityActions.clear())}>Clear</button><ol id="activity" aria-live="polite">{activity.entries.map(entry => <li key={entry.id} className={entry.kind}><span className="log-dot" /><span>{entry.text}</span></li>)}</ol></details>
      <footer className="page-footer"><span>LightVM · Controller workspace</span><span>Made for a little code and a lot of light.</span></footer>
    </main>
  </>;
}

function Controllers({ onConnected }: { onConnected: () => void }) {
  const state = useAppSelector(state => state.controller), busy = useAppSelector(state => Boolean(state.activity.operation));
  const { controllers } = useRuntime();
  const [address, setAddress] = useState(''), [selected, setSelected] = useState('');
  const hasHttp = state.paths.some(path => path.key === 'http'), hasUsb = state.paths.some(path => path.key === 'usb');
  const availableSerial = isSecureContext && Boolean(navigator.serial);
  useEffect(() => {
    if (address) return;
    const suggested = controllers.suggestedAddress() || state.nearby[0]?.url;
    if (suggested) { setAddress(suggested); setSelected(state.nearby.find(item => item.url === suggested)?.controller_id || ''); }
  }, [address, state.nearby, state.networkStatus, controllers]);
  async function connect(path: Path) { if (await controllers.connect(path, address)) onConnected(); }
  return <>
    <div className="intro"><div><p className="eyebrow">START WITH YOUR CONTROLLER</p><h1>Find your lights.</h1><p>Connect once, then move between programming and configuration.</p></div><Link className="quiet-link" to="/develop">Open the editor without a controller →</Link></div>
    {state.paths.length > 0 && <section className="connected-card"><div><span className="status-dot connected" /> <strong>{state.description?.board}</strong><p className="helper">Your controller is connected. You can add another path to the same controller.</p></div><Link className="button secondary" to={controllerRoute(state.description!.controller_id, 'develop')}>Open workspace</Link><button className="quiet" disabled={busy} onClick={() => void controllers.disconnectAll()}>Disconnect controller</button></section>}
    <div className="controller-picker-grid">
      <section className="card controller-card"><div className="card-heading"><h2>On your network</h2><button id="discover-controllers" className="quiet small" disabled={busy || state.discovering} onClick={() => void controllers.find([address])}>Refresh</button></div>
        <p className="muted">Choose a LightVM controller on the same local network.</p>
        <label htmlFor="nearby-controllers">Nearby controllers</label><select id="nearby-controllers" value={selected} disabled={busy || !state.nearby.length || hasHttp} onChange={event => { setSelected(event.target.value); const controller = state.nearby.find(item => item.controller_id === event.target.value); if (controller) setAddress(controller.url); }}><option value="">Choose a controller</option>{state.nearby.map(controller => <option key={controller.controller_id} value={controller.controller_id}>{controller.board + ' · ' + controller.hostname}</option>)}</select>
        <p id="discovery-state" className="helper" role="status">{state.discoveryMessage}</p>
        <label htmlFor="http-address">Controller HTTP address</label><input id="http-address" type="url" value={address} onChange={event => { setAddress(event.target.value); setSelected(''); }} placeholder="http://lightvm.local" autoComplete="off" />
        <button id="connect-http" className="primary full" disabled={busy || hasHttp || !address} onClick={() => void connect('http')}>Connect controller <span aria-hidden="true">→</span></button>
        <p className="helper">Browser local-network permission may be required. A direct address also works.</p>
      </section>
      <section className="card controller-card"><div className="card-heading"><h2>Plugged in over USB</h2></div><p className="muted">Connect a Pico or ESP32 running LightVM. The browser will ask you to choose its USB port.</p>
        <button id="connect" className="primary full" disabled={busy || hasUsb || !availableSerial} onClick={() => void connect('usb')}>Choose USB controller <span aria-hidden="true">↗</span></button>
        <p className="helper">{!isSecureContext ? 'USB requires HTTPS or localhost.' : !navigator.serial ? 'USB requires a browser with Web Serial, such as desktop Chrome or Edge.' : 'Close other serial clients before connecting. Disconnecting leaves playback running.'}</p>
        <div className="setup-entry"><h3>Starting with a new board?</h3><p className="muted">Install LightVM firmware on a supported XIAO ESP32 board.</p><Link to="/firmware">Firmware installation →</Link></div>
      </section>
    </div>
  </>;
}
