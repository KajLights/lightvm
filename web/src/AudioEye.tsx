import { useEffect, useRef, useState } from 'react';
import { Link } from 'react-router-dom';
import { useRuntime } from './runtime';
import { controllerActions, useAppDispatch, useAppSelector } from './store';
import { AudioEyeSession, audioEyeTarget } from './audio-eye-session';
import { MeydaFeatureExtractor } from './meyda-features';
import { MeydaComparison } from './MeydaComparison';
import { AUDIO_SIGNALS, MEYDA_SIGNALS, DEFAULT_MAPPINGS, DEFAULT_RESPONSE, EYE_INPUTS, MicrophoneAudio, SILENCE,
  mapAudioToEye, type AudioAnalysis, type AudioFeatures, type AudioResponse, type EyeInput, type EyeMappings } from './audio-features';

const SIGNAL_LABELS = { level: 'Level', bass: 'Bass', mid: 'Midrange', treble: 'Treble', centroid: 'Spectral centre', onset: 'Onset pulse', flatness: 'Spectral flatness', rolloff: 'Spectral roll-off', zcr: 'Zero-crossing rate' };

export function AudioEye({ engine = 'web-audio' }: { engine?: 'web-audio' | 'meyda' }) {
  const meyda = engine === 'meyda', signals = meyda ? [...AUDIO_SIGNALS, ...MEYDA_SIGNALS] : AUDIO_SIGNALS;
  const { controllers } = useRuntime(), dispatch = useAppDispatch();
  const controller = useAppSelector(state => state.controller), busy = useAppSelector(state => Boolean(state.activity.operation));
  const [features, setFeatures] = useState<AudioFeatures>({ ...SILENCE });
  const [analysis, setAnalysis] = useState<AudioAnalysis>();
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
    setListening('idle'); setFeatures({ ...SILENCE }); setAnalysis(undefined); void release();
  }
  useEffect(() => {
    mounted.current = true;
    const leave = () => {
      capture.current?.stop(); capture.current = undefined;
      if (mounted.current) { setListening('idle'); setFeatures({ ...SILENCE }); setAnalysis(undefined); }
      void release();
    };
    const hidden = () => { if (document.hidden) leave(); };
    document.addEventListener('visibilitychange', hidden); window.addEventListener('pagehide', leave);
    return () => {
      mounted.current = false; document.removeEventListener('visibilitychange', hidden); window.removeEventListener('pagehide', leave);
      capture.current?.stop(); void release();
    };
  }, []);
  // Never transfer an old audio session to a new output, program generation or command path.
  useEffect(() => () => { void release(); }, [client, identity]);
  useEffect(() => { session.current?.observe(controller.programState); }, [controller.programState]);
  useEffect(() => {
    stop();
    if (!meyda) setMappings(current => Object.fromEntries(EYE_INPUTS.map(name => [name,
      (AUDIO_SIGNALS as readonly string[]).includes(current[name].signal) ? current[name] : DEFAULT_MAPPINGS[name],
    ])) as EyeMappings);
  }, [engine]);

  async function start() {
    setError(''); setListening('starting');
    const extractor = meyda ? new MeydaFeatureExtractor() : undefined;
    const input = new MicrophoneAudio((frame, details) => {
      if (capture.current !== input) return;
      setFeatures(frame); setAnalysis(details); session.current?.publish(mapAudioToEye(frame, config.current.mappings));
    }, problem => {
      if (capture.current !== input) return;
      stop(); setError(problem.message);
    }, () => config.current.response, extractor?.extract.bind(extractor));
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
    <div className="intro"><div><p className="eyebrow">{meyda ? 'MEYDA AUDIO DEMO' : 'AUDIO INPUT DEMO'}</p><h1>{meyda ? 'Hear the difference with Meyda.' : 'Let the Eye listen.'}</h1><p>{meyda ? 'Compare richer audio features against Web Audio on the same live sound.' : 'Turn live sound into position, size, colour and light.'}</p></div></div>
    <nav className="workspace-tabs audio-engine-tabs" aria-label="Audio analyzer"><Link to="/audio" aria-current={!meyda ? 'page' : undefined}>Web Audio</Link><Link to="/meyda" aria-current={meyda ? 'page' : undefined}>Meyda comparison</Link></nav>
    <div className="audio-layout">
      <section className="card audio-source"><h2>1. Listen</h2>
        <p className="muted">Use a microphone, USB audio interface or loopback input. Audio is analysed in this browser; only four control values go to the lights.</p>
        <label htmlFor="audio-device">Audio input</label>
        <select id="audio-device" value={device} disabled={listening !== 'idle'} onChange={event => setDevice(event.target.value)}>
          <option value="">System default</option>{devices.filter(item => item.deviceId !== 'default').map((item, index) => <option key={item.deviceId} value={item.deviceId}>{item.label || 'Audio input ' + (index + 1)}</option>)}
        </select>
        <p className="helper">Start once to reveal available input names. To change input, stop and choose another.</p>
        <div className="audio-buttons"><button className="primary" disabled={listening !== 'idle'} onClick={() => void start()}>{listening === 'starting' ? 'Waiting for microphone…' : 'Start listening'}</button><button disabled={listening === 'idle'} onClick={stop}>Stop listening</button></div>
        <p role="status">{listening === 'active' ? 'Listening · about 30 updates per second' : listening === 'starting' ? 'Choose an audio input in the browser prompt.' : 'Microphone off'}</p>
        <label htmlFor="audio-gain">Sensitivity · {response.gainDb} dB</label><input id="audio-gain" type="range" min="-12" max="36" value={response.gainDb} onChange={event => setResponse({ ...response, gainDb: Number(event.target.value) })} />
        <label htmlFor="audio-floor">Noise floor · {response.floorDb} dB</label><input id="audio-floor" type="range" min="-70" max="-20" value={response.floorDb} onChange={event => setResponse({ ...response, floorDb: Number(event.target.value) })} />
        <label htmlFor="audio-release">Release · {response.releaseMs} ms</label><input id="audio-release" type="range" min="40" max="1000" step="10" value={response.releaseMs} onChange={event => setResponse({ ...response, releaseMs: Number(event.target.value) })} />
        <p className="helper">Requires HTTPS or localhost and microphone permission. For a controller served over HTTP, run the editor on localhost and connect to it from there. Listening stops when this page is hidden or closed.</p>
      </section>
      <div className="audio-results">
        <section className="card"><div className="card-heading"><h2>2. Watch the response</h2><span className="badge">{meyda ? 'Meyda preview' : 'Local preview'}</span></div>
          <div className="audio-eye-strip" role="img" aria-label={`Eye preview: position ${eye.position.toFixed(2)}, width ${eye.width.toFixed(2)}, brightness ${eye.brightness.toFixed(2)}`}>
            <div className="audio-eye-brush" style={{ left: (eye.position * (1 - eye.width) * 100) + '%', width: (eye.width * 100) + '%', opacity: listening === 'active' ? eye.brightness : 0, background: `linear-gradient(90deg, hsl(${eye.hue * 360} 100% 17%), hsl(${eye.hue * 360} 100% 50%), hsl(${eye.hue * 360} 100% 17%))` }} />
          </div>
          <p className="helper">Schematic strip preview using the Eye’s normalized controls. The controller renders the actual sprite.</p>
          <div className="audio-meters">{signals.map(name => <div key={name}><label htmlFor={'audio-meter-' + name}>{SIGNAL_LABELS[name]}</label><meter id={'audio-meter-' + name} min="0" max="1" value={features[name]} /><output>{features[name].toFixed(2)}</output></div>)}</div>
          <p className="helper">Bass: 40–250 Hz · Midrange: 250–2,000 Hz · Treble: 2,000–12,000 Hz. Onset is a simple level transient pulse; it does not estimate BPM or predict beats.</p>
        </section>
        {meyda && <MeydaComparison analysis={analysis} mappings={mappings} listening={listening === 'active'} />}
        <section className="card"><h2>3. Wire the Eye</h2><p className="muted">Choose a signal and its output range. Swap the range ends to invert the response.</p>
          <div className="audio-mappings">{EYE_INPUTS.map(name => <fieldset key={name}><legend>{name[0].toUpperCase() + name.slice(1)} <output>{eye[name].toFixed(2)}</output></legend>
            <label htmlFor={'audio-map-' + name}>Signal for {name}</label><select id={'audio-map-' + name} value={mappings[name].signal} onChange={event => patch(name, { signal: event.target.value as EyeMappings[EyeInput]['signal'] })}>{signals.map(signal => <option key={signal} value={signal}>{SIGNAL_LABELS[signal]}</option>)}</select>
            <div className="audio-range">{(['minimum', 'maximum'] as const).map((end, index) => <label key={end}>{index ? 'To' : 'From'}<input aria-label={`${name} ${index ? 'to' : 'from'}`} type="number" min="0" max="1" step="0.01" value={mappings[name][end]} onChange={event => { if (event.target.value !== '') patch(name, { [end]: Math.max(0, Math.min(1, Number(event.target.value))) }); }} /></label>)}</div>
          </fieldset>)}</div>
          <button className="quiet" onClick={() => setMappings(DEFAULT_MAPPINGS)}>Reset mappings</button>
          <div className="audio-controller">
            <p>{!client ? 'Connect a controller and preview eye.py in Develop to drive real lights.' : !supported ? 'This controller needs the live animation inputs firmware.' : !target ? 'Preview eye.py in Develop, then return here.' : `Target: ${target.output || controller.selectedOutput || 'output'} · ${target.slot}${target.layer ? ' · ' + target.layer : ''} · generation ${target.generation}`}</p>
            <div className="audio-buttons"><button className="primary" disabled={listening !== 'active' || forwarding !== 'idle' || !supported || !target || !client || busy} onClick={() => void drive()}>{forwarding === 'starting' ? 'Connecting…' : 'Drive running Eye'}</button><button disabled={forwarding !== 'active'} onClick={() => void release()}>{forwarding === 'stopping' ? 'Restoring…' : 'Release Eye'}</button>
              <Link to={controller.description ? '/controllers/' + encodeURIComponent(controller.description.controller_id) + '/develop' : '/develop'}>Open Develop →</Link></div>
            <p className="helper">{forwarding === 'active' ? 'Driving the Eye. ' : ''}Taking control preserves the running animation. Release restores its previous input bindings if nobody has rewired it. Connection loss uses the Eye’s declared defaults after one second.</p>
          </div>
        </section>
      </div>
    </div>
    {error && <p className="notice" role="alert">{error}</p>}
  </div>;
}
