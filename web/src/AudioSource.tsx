import { useEffect, useRef, useState } from 'react';
import { Link } from 'react-router-dom';
import { useRuntime } from './runtime';
import { useAppSelector } from './store';
import { AudioSourceSession } from './audio-source-session';
import { AUDIO_SIGNALS, DEFAULT_RESPONSE, MicrophoneAudio, SILENCE, type AudioFeatures, type AudioResponse } from './audio-features';
import { MUSIC_LABELS } from './InputBindingControl';
import { LiveInputs } from './LiveInputs';

export function AudioSource({ active = true }: { active?: boolean }) {
  const { controllers } = useRuntime();
  const controller = useAppSelector(state => state.controller);
  const [features, setFeatures] = useState<AudioFeatures>({ ...SILENCE });
  const [response, setResponse] = useState<AudioResponse>({ ...DEFAULT_RESPONSE });
  const config = useRef(response); config.current = response;
  const [devices, setDevices] = useState<MediaDeviceInfo[]>([]), [device, setDevice] = useState('');
  const [listening, setListening] = useState<'idle' | 'starting' | 'active'>('idle');
  const [forwarding, setForwarding] = useState(false), [error, setError] = useState('');
  const capture = useRef<MicrophoneAudio | undefined>(undefined), session = useRef<AudioSourceSession | undefined>(undefined);
  const mounted = useRef(true), client = controllers.client;
  function release() {
    const previous = session.current; session.current = undefined;
    if (mounted.current) setForwarding(false);
    void previous?.stop();
  }
  function stop() {
    capture.current?.stop(); capture.current = undefined; release();
    if (mounted.current) { setListening('idle'); setFeatures({ ...SILENCE }); }
  }
  useEffect(() => {
    mounted.current = true;
    const hidden = () => { if (document.hidden) stop(); };
    document.addEventListener('visibilitychange', hidden); window.addEventListener('pagehide', stop);
    return () => { mounted.current = false; document.removeEventListener('visibilitychange', hidden); window.removeEventListener('pagehide', stop); stop(); };
  }, []);
  // Navigation and preset changes leave the source running; reconnect never reclaims it silently.
  useEffect(() => () => release(), [client, controller.description?.boot_id]);
  async function start() {
    setError(''); setListening('starting');
    const input = new MicrophoneAudio(frame => {
      if (capture.current !== input) return;
      setFeatures(frame); session.current?.publish(frame);
    }, problem => { if (capture.current === input) { stop(); setError(problem.message); } }, () => config.current);
    capture.current = input;
    try {
      await input.start(device);
      if (capture.current !== input) return;
      setListening('active');
      try { const all = await navigator.mediaDevices.enumerateDevices(); if (capture.current === input) setDevices(all.filter(item => item.kind === 'audioinput')); } catch { /* Capture does not require a device list. */ }
    } catch (problem) {
      if (capture.current === input) { stop(); setError((problem as Error).message); }
    }
  }
  async function publish() {
    if (!client || !controller.description || listening !== 'active' || session.current) return;
    setError('');
    const next = new AudioSourceSession(client, controller.description.boot_id, problem => { if (mounted.current) { release(); setError(problem.message); } });
    session.current = next;
    try { await next.start(); if (session.current === next) { next.publish(features); setForwarding(true); } }
    catch (problem) { if (session.current === next) { release(); setError((problem as Error).message); } }
  }
  if (!active) return listening === 'idle' ? null : <aside className="card" aria-label="Active music source">Music input {forwarding ? 'is sending to the controller' : 'is listening locally'}. <Link to="/audio">Audio controls</Link> <button onClick={stop}>Stop listening</button></aside>;
  return <div className="audio-demo"><div className="intro"><div><p className="eyebrow">04 / MUSIC SOURCE</p><h1>Find your rhythm.</h1><p>Use music to control any animation.</p></div></div>
    <section className="audio-master card"><h2>Audio source</h2><label htmlFor="audio-device">Audio input</label><select id="audio-device" value={device} disabled={listening !== 'idle'} onChange={event => setDevice(event.target.value)}><option value="">System default</option>{devices.filter(item => item.deviceId !== 'default').map((item, index) => <option key={item.deviceId} value={item.deviceId}>{item.label || 'Audio input ' + (index + 1)}</option>)}</select>
      <div className="audio-buttons"><button className="primary" disabled={listening !== 'idle'} onClick={() => void start()}>{listening === 'starting' ? 'Waiting for microphone…' : 'Start listening'}</button><button disabled={listening === 'idle'} onClick={stop}>Stop listening</button></div>
      <p role="status">{listening === 'active' ? 'Listening · local audio analysis' : 'Microphone off'}</p>
      <div className="master-faders">{([['gainDb', 'Sensitivity', -12, 36, 1, 'dB'], ['floorDb', 'Noise floor', -70, -20, 1, 'dB'], ['releaseMs', 'Release', 40, 1000, 10, 'ms']] as const).map(([key, label, min, max, step, unit]) => <div className="master-fader" key={key}><label htmlFor={'audio-' + key}>{label}</label><input id={'audio-' + key} className="vertical-fader" type="range" min={min} max={max} step={step} value={response[key]} onChange={event => setResponse({ ...response, [key]: Number(event.target.value) })} /><output>{response[key]} {unit}</output></div>)}</div>
      <div className="audio-meters">{AUDIO_SIGNALS.map((name, index) => <div key={name}><label htmlFor={'audio-meter-' + name}>{MUSIC_LABELS[index]}</label><meter id={'audio-meter-' + name} min="0" max="1" value={features[name]} /><output>{Math.round(features[name] * 100)}%</output></div>)}</div>
      <p className="helper">Audio stays in this browser. Only signal levels are sent to the controller. Listening continues while you use the editor or mixer and stops when this browser tab is hidden.</p>
    </section>
    <section className="audio-controller card"><h2>Music for your lights</h2><p className="helper">Choose Music in an animation’s input controls below or save those bindings in a preset. One music source can drive several animations.</p>
      <div className="audio-buttons"><button className="primary" disabled={listening !== 'active' || forwarding || !client || !controller.description?.live_inputs} onClick={() => void publish()}>Send music to controller</button><button disabled={!forwarding} onClick={release}>Stop sending</button></div>
      <p role="status">{forwarding ? 'Sending music features' : 'Music forwarding off'}</p>
      <p className="helper">Stopping leaves your bindings in place; each input uses its configured fallback.</p>
    </section><LiveInputs active={active} />{error && <p className="notice" role="alert">{error}</p>}
  </div>;
}
