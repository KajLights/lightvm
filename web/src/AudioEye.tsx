import { useEffect, useRef, useState } from 'react';
import { Link } from 'react-router-dom';
import { useRuntime } from './runtime';
import { controllerActions, useAppDispatch, useAppSelector } from './store';
import { AudioEyeSession, audioEyeTarget } from './audio-eye-session';
import { AUDIO_SIGNALS, DEFAULT_MAPPINGS, DEFAULT_RESPONSE, EYE_INPUTS, MicrophoneAudio, SILENCE,
  mapAudioToEye, normalizeAudioSignal, type AudioFeatures, type AudioResponse, type EyeInput, type EyeMappings } from './audio-features';

const SIGNAL_LABELS = { level: 'Level', bass: 'Bass', mid: 'Midrange', treble: 'Treble', centroid: 'Spectral centre', onset: 'Onset pulse' };

export function AudioEye({ active = true }: { active?: boolean }) {
  const { controllers } = useRuntime(), dispatch = useAppDispatch();
  const controller = useAppSelector(state => state.controller), busy = useAppSelector(state => Boolean(state.activity.operation));
  const [features, setFeatures] = useState<AudioFeatures>({ ...SILENCE });
  const [response, setResponse] = useState<AudioResponse>({ ...DEFAULT_RESPONSE });
  const [mappings, setMappings] = useState<EyeMappings>(DEFAULT_MAPPINGS);
  const [devices, setDevices] = useState<MediaDeviceInfo[]>([]), [device, setDevice] = useState('');
  const [listening, setListening] = useState<'idle' | 'starting' | 'active'>('idle');
  const [forwarding, setForwarding] = useState<'idle' | 'starting' | 'active' | 'stopping'>('idle');
  const [error, setError] = useState('');
  const config = useRef({ response, mappings }); config.current = { response, mappings };
  const capture = useRef<MicrophoneAudio | undefined>(undefined), session = useRef<AudioEyeSession | undefined>(undefined);
  const mounted = useRef(true);
  const eye = mapAudioToEye(features, mappings), target = audioEyeTarget(controller.programState);
  const client = controllers.client;
  const identity = JSON.stringify([controller.description?.boot_id, controller.selectedOutput, target?.output, target?.slot, target?.layer, target?.generation]);
  const supported = controller.description && 'live_inputs' in controller.description && controller.description.live_inputs === true;

  async function release() {
    const previous = session.current; session.current = undefined;
    if (!previous) return;
    if (mounted.current) setForwarding('stopping');
    try { await previous.stop(); }
    catch (problem) { if (mounted.current) setError('Audio stopped. Previous bindings could not be restored: ' + (problem as Error).message); }
    finally { if (mounted.current) setForwarding('idle'); }
  }
  function stop() {
    capture.current?.stop(); capture.current = undefined;
    setListening('idle'); setFeatures({ ...SILENCE }); void release();
  }
  useEffect(() => {
    mounted.current = true;
    const leave = () => {
      capture.current?.stop(); capture.current = undefined;
      if (mounted.current) { setListening('idle'); setFeatures({ ...SILENCE }); }
      void release();
    };
    const hidden = () => { if (document.hidden) leave(); };
    document.addEventListener('visibilitychange', hidden); window.addEventListener('pagehide', leave);
    return () => {
      mounted.current = false; document.removeEventListener('visibilitychange', hidden); window.removeEventListener('pagehide', leave);
      capture.current?.stop(); void release();
    };
  }, []);
  useEffect(() => { if (!active) stop(); }, [active]);
  // Never transfer an old audio session to a new output, program generation or command path.
  useEffect(() => () => { void release(); }, [client, identity]);
  useEffect(() => { session.current?.observe(controller.programState); }, [controller.programState]);

  async function start() {
    setError(''); setListening('starting');
    const input = new MicrophoneAudio(frame => {
      if (capture.current !== input) return;
      setFeatures(frame); session.current?.publish(mapAudioToEye(frame, config.current.mappings));
    }, problem => {
      if (capture.current !== input) return;
      stop(); setError(problem.message);
    }, () => config.current.response);
    capture.current = input;
    try {
      await input.start(device);
      if (capture.current !== input) return;
      setListening('active');
      // Device names become available after the browser grants microphone access.
      try {
        const all = await navigator.mediaDevices.enumerateDevices();
        if (capture.current === input) setDevices(all.filter(item => item.kind === 'audioinput'));
      } catch { /* Capture remains usable if device enumeration is unavailable. */ }
    } catch (problem) {
      if (capture.current === input) { capture.current = undefined; setListening('idle'); setError((problem as Error).message); }
    }
  }
  async function drive() {
    if (!target || !client || !controller.description || listening !== 'active') return;
    setError(''); setForwarding('starting');
    let next: AudioEyeSession | undefined;
    const success = await controllers.run('Connecting audio to Eye', async () => {
      next = new AudioEyeSession(client, target, controller.description!.boot_id, state => {
        const current = audioEyeTarget(controllers.snapshot.controller.programState);
        if (client === controllers.client && current?.generation === target.generation &&
          current?.output === target.output && current?.slot === target.slot && current?.layer === target.layer) {
          dispatch(controllerActions.programState(state));
        }
      }, problem => { if (mounted.current) { setError('Audio forwarding stopped: ' + problem.message); void release(); } });
      session.current = next;
      try { await next.start(); }
      catch (problem) { await release(); throw problem; }
    });
    if (success && next && session.current === next) {
      next.publish(mapAudioToEye(features, config.current.mappings)); setForwarding('active');
    } else if (mounted.current && !session.current) setForwarding('idle');
  }
  function patch(name: EyeInput, change: Partial<EyeMappings[EyeInput]>) {
    setMappings(current => ({ ...current, [name]: { ...current[name], ...change } }));
  }
  return <div className="audio-demo">
    <div className="intro"><div><p className="eyebrow">04 / AUDIO PLAYGROUND</p><h1>Find your rhythm.</h1><p>Listen, shape the response, and tune the lights by feel.</p></div><span className={'badge ' + (listening === 'active' ? 'ready' : '')}><span className="status-dot" />{listening === 'active' ? 'LISTENING' : 'AUDIO OFF'}</span></div>
    <section className="audio-monitor card"><div className="card-heading"><h2>Eye / local monitor</h2><span className="helper">{forwarding === 'active' ? 'Sending to ' + target?.output : 'Preview only'}</span></div>
      <div className="audio-eye-strip" role="img" aria-label={`Eye preview: position ${eye.position.toFixed(2)}, width ${eye.width.toFixed(2)}, brightness ${eye.brightness.toFixed(2)}`}><div className="audio-eye-brush" style={{ left: (eye.position * (1 - eye.width) * 100) + '%', width: (eye.width * 100) + '%', opacity: listening === 'active' ? eye.brightness : 0, background: `linear-gradient(90deg, hsl(${eye.hue * 360} 100% 17%), hsl(${eye.hue * 360} 100% 50%), hsl(${eye.hue * 360} 100% 17%))` }} /></div>
    </section>
    <div className="audio-console">
      <section className="audio-master card"><p className="desk-label">INPUT / MASTER</p><h2>Audio source</h2><label htmlFor="audio-device">Audio input</label>
        <select id="audio-device" value={device} disabled={listening !== 'idle'} onChange={event => setDevice(event.target.value)}><option value="">System default</option>{devices.filter(item => item.deviceId !== 'default').map((item, index) => <option key={item.deviceId} value={item.deviceId}>{item.label || 'Audio input ' + (index + 1)}</option>)}</select>
        <div className="audio-buttons"><button className="primary" disabled={listening !== 'idle'} onClick={() => void start()}>{listening === 'starting' ? 'Waiting for microphone…' : 'Start listening'}</button><button disabled={listening === 'idle'} onClick={stop}>Stop listening</button></div>
        <p className="helper" role="status">{listening === 'active' ? 'Listening · local audio analysis' : 'Microphone off · start to select an input'}</p>
        <div className="master-faders">{([
          ['gainDb', 'Sensitivity', -12, 36, 1, 'dB'], ['floorDb', 'Noise floor', -70, -20, 1, 'dB'], ['releaseMs', 'Release', 40, 1000, 10, 'ms'],
        ] as const).map(([key, label, min, max, step, unit]) => <div className="master-fader" key={key}><label htmlFor={'audio-' + key}>{label}</label><input id={'audio-' + key} className="vertical-fader" type="range" min={min} max={max} step={step} value={response[key]} onChange={event => setResponse({ ...response, [key]: Number(event.target.value) })} /><output>{response[key]} {unit}</output></div>)}</div>
        <details><summary>Signal meters</summary><div className="audio-meters">{AUDIO_SIGNALS.map(name => <div key={name}><label htmlFor={'audio-meter-' + name}>{SIGNAL_LABELS[name]}</label><meter id={'audio-meter-' + name} min="0" max="1" value={features[name]} /><output>{Math.round(features[name] * 100)}%</output></div>)}</div><p className="helper">Bass 40–250 Hz · Mid 250–2,000 Hz · Treble 2–12 kHz. Onset follows transients.</p></details>
        <p className="helper">Audio stays in this browser. Capture stops when this page is hidden.</p>
      </section>
      <section className="audio-channels" aria-label="Response channel strips">{EYE_INPUTS.map((name, index) => {
        const mapping = mappings[name], raw = features[mapping.signal], normalized = normalizeAudioSignal(raw, mapping);
        return <fieldset key={name} className={'audio-channel channel-' + name}><legend><span>{String(index + 1).padStart(2, '0')}</span>{name[0].toUpperCase() + name.slice(1)}</legend>
          <label htmlFor={'audio-map-' + name}>Signal for {name}</label><select id={'audio-map-' + name} value={mapping.signal} onChange={event => patch(name, { signal: event.target.value as EyeMappings[EyeInput]['signal'] })}>{AUDIO_SIGNALS.map(signal => <option key={signal} value={signal}>{SIGNAL_LABELS[signal]}</option>)}</select>
          <div className="channel-readout"><span>INPUT</span><output>{(raw * 100).toFixed(1)}<small>%</small></output></div>
          <label className="range-caption">Input window (%)</label><div className="audio-range">{(['inputMinimum', 'inputMaximum'] as const).map((end, i) => <label key={end}>{i ? 'High' : 'Low'}<input aria-label={`${name} input ${i ? 'maximum' : 'minimum'}`} type="number" min={i ? Math.round(mapping.inputMinimum * 100) + 1 : 0} max={i ? 100 : Math.round(mapping.inputMaximum * 100) - 1} step="1" value={Math.round(mapping[end] * 100)} onChange={event => {
            if (event.target.value === '' || !Number.isFinite(Number(event.target.value))) return;
            const value = Math.max(0, Math.min(1, Number(event.target.value) / 100));
            patch(name, { [end]: i ? Math.max(mapping.inputMinimum + 0.01, value) : Math.min(mapping.inputMaximum - 0.01, value) });
          }} /></label>)}</div>
          <div className="channel-meter-pair"><div><span>TRIM</span><meter aria-label={name + ' normalized input'} min="0" max="1" value={normalized} /></div><div><span>OUT</span><meter aria-label={name + ' output level'} min="0" max="1" value={eye[name]} /></div></div>
          <output className="channel-output" aria-label={name + ' output'}>{Math.round(eye[name] * 100)}<small>%</small></output>
          <label className="range-caption">Output range (%)</label><div className="audio-range">{(['minimum', 'maximum'] as const).map((end, i) => <label key={end}>{i ? 'To' : 'From'}<input aria-label={`${name} ${i ? 'to' : 'from'}`} type="number" min="0" max="100" step="1" value={Math.round(mapping[end] * 100)} onChange={event => { if (event.target.value !== '' && Number.isFinite(Number(event.target.value))) patch(name, { [end]: Math.max(0, Math.min(1, Number(event.target.value) / 100)) }); }} /></label>)}</div>
          <button className="quiet full" onClick={() => patch(name, { minimum: mapping.maximum, maximum: mapping.minimum })}>Invert ↕</button>
        </fieldset>;
      })}</section>
    </div>
    <div className="audio-tuning-footer"><p className="helper">Input below Low becomes 0%; above High becomes 100%. Narrow the window to bring out small changes.</p><button className="quiet" onClick={() => { setMappings(DEFAULT_MAPPINGS); setResponse({ ...DEFAULT_RESPONSE }); }}>Reset tuning</button></div>
    <section className="audio-controller card"><div><h2>Send to lights</h2><p className="helper">{!client ? 'Connect a device, then preview eye.py in Editor.' : !supported ? 'This controller needs live animation inputs firmware.' : !target ? 'Preview eye.py in Editor, then return here.' : `Ready on ${target.output || controller.selectedOutput || 'output'} · ${target.slot}${target.layer ? ' · ' + target.layer : ''}`}</p></div>
      <div className="audio-buttons"><button className="primary" disabled={listening !== 'active' || forwarding !== 'idle' || !supported || !target || !client || busy} onClick={() => void drive()}>{forwarding === 'starting' ? 'Connecting…' : 'Drive running Eye'}</button><button disabled={forwarding !== 'active'} onClick={() => void release()}>{forwarding === 'stopping' ? 'Restoring…' : 'Release Eye'}</button><Link to={controller.description ? '/controllers/' + encodeURIComponent(controller.description.controller_id) + '/editor' : '/editor'}>Open Editor →</Link></div>
      <p className="helper">Release restores the previous input bindings. Connection loss uses the animation defaults after one second.</p>
    </section>
    {error && <p className="notice" role="alert">{error}</p>}
  </div>;
}
