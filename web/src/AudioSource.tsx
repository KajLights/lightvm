import { useEffect, useRef, useState } from 'react';
import { Link } from 'react-router-dom';
import { useRuntime } from './runtime';
import { useAppSelector } from './store';
import { AudioSourceSession } from './audio-source-session';
import { AUDIO_SIGNALS, defaultParameters, loadAudioAssets, MicrophoneAudio, readAudioFixture,
  type AnalysisFrame, type AudioFixture, type AudioManifest, type AudioParameters, type AudioSnapshot } from './audio-features';
import { MUSIC_LABELS } from './InputBindingControl';
import { LiveInputs } from './LiveInputs';
const labels: Record<keyof AudioParameters, string> = { gain_db: 'Source gain', floor_db: 'Noise floor', attack_ms: 'Attack', release_ms: 'Release', band_count: 'Spectrum bands', onset_threshold: 'Onset threshold', onset_refractory_ms: 'Refractory period' };
const units: Partial<Record<keyof AudioParameters, string>> = { gain_db: 'dB', floor_db: 'dB', attack_ms: 'ms', release_ms: 'ms', onset_refractory_ms: 'ms' };
function AnalysisPanel({ profile, title, frames }: { profile: number; title: string; frames: AnalysisFrame[] }) {
  const frame = frames.at(-1), values = frame?.features;
  const end = frame?.audioUs ?? 0n, start = end - 2500000n, x = (time: bigint) => Number(time - start) * 600 / 2500000;
  const visible = frames.filter(f => f.audioUs >= start);
  const points = (field: 'novelty' | 'threshold' | 'level') => visible.map(f => `${x(f.audioUs)},${100 - Math.min(1, field === 'level' ? f.features.level : f[field]) * 95}`).join(' ');
  const occurrences = [...new Map(visible.flatMap(f => f.events.filter(e => e.timeUs >= start).map(e => [e.id, e] as const))).values()];
  return <section className="card audio-analysis" aria-label={title}><h2>{title}</h2>
    <div className="audio-meters">{AUDIO_SIGNALS.map((name, i) => <div key={name}><label htmlFor={`meter-${profile}-${name}`}>{MUSIC_LABELS[i]}</label><meter id={`meter-${profile}-${name}`} min="0" max="1" value={values?.[name] ?? 0} /><output>{Math.round((values?.[name] ?? 0) * 100)}%</output></div>)}</div>
    <svg className="audio-spectrum" viewBox="0 0 600 100" role="img" aria-label={`${title} spectrum`}>{Array.from(frame?.bands ?? []).map((level, i, bands) => <rect key={i} x={i * 600 / bands.length} y={100 - level * 100} width={Math.max(1, 600 / bands.length - 2)} height={level * 100} fill={`hsl(${200 + i * 90 / bands.length} 75% 55%)`} />)}</svg>
    <svg className="audio-timeline" viewBox="0 0 600 105" role="img" aria-label={`${title} onset timeline`}>
      {occurrences.map(e => <line key={String(e.id)} x1={x(e.timeUs)} x2={x(e.timeUs)} y1={100 - e.strength * 100} y2="100" stroke="#eaaa35" strokeWidth="2" />)}
      <polyline points={points('level')} fill="none" stroke="#8cabc3" strokeWidth="1" /><polyline points={points('novelty')} fill="none" stroke="#55b9b0" strokeWidth="2" /><polyline points={points('threshold')} fill="none" stroke="#dc7e8c" strokeWidth="2" />
    </svg><div className="audio-time-labels"><span>−2.5 seconds</span><span>Now</span></div>
    <p className="audio-legend"><span>Level</span><span>Novelty</span><span>Threshold</span><span>Onset strength</span></p>
    <p className="helper">Both profiles use identical PCM and calibration. The export retains every analysis hop for up to 64 seconds.</p>
  </section>;
}
export function AudioSource({ active = true }: { active?: boolean }) {
  const { controllers } = useRuntime(); const controller = useAppSelector(state => state.controller);
  const [manifest, setManifest] = useState<AudioManifest>(), [parameters, setParameters] = useState<AudioParameters>();
  const [profile, setProfile] = useState(1), [compare, setCompare] = useState(false), [both, setBoth] = useState(false), [name, setName] = useState('music');
  const [channel, setChannel] = useState(-1), [fixture, setFixture] = useState<AudioFixture>(), [device, setDevice] = useState(''), [devices, setDevices] = useState<MediaDeviceInfo[]>([]);
  const [listening, setListening] = useState<'idle' | 'starting' | 'active' | 'finished'>('idle'), [forwarding, setForwarding] = useState(false), [error, setError] = useState('');
  const [frames, setFrames] = useState<AnalysisFrame[][]>([[], []]), [captureInfo, setCaptureInfo] = useState<Record<string, unknown>>({});
  const capture = useRef<MicrophoneAudio | undefined>(undefined), lastCapture = useRef<MicrophoneAudio | undefined>(undefined), sessions = useRef<{ profile: number; session: AudioSourceSession }[]>([]);
  const latest = useRef<AudioSnapshot | undefined>(undefined), historyEpoch = useRef(-1), mounted = useRef(true), client = controllers.client;
  const release = () => { const previous = sessions.current; sessions.current = []; if (mounted.current) setForwarding(false); for (const p of previous) void p.session.stop(); };
  const stop = () => { const input = capture.current; capture.current = undefined; if (input) { lastCapture.current = input; void input.stop(); } release(); if (mounted.current) setListening('idle'); };
  useEffect(() => {
    mounted.current = true;
    void loadAudioAssets().then(({ manifest }) => { if (mounted.current) { setManifest(manifest); setParameters(defaultParameters(manifest)); } }).catch(e => { if (mounted.current) setError(e.message); });
    const hidden = () => { if (document.hidden) stop(); };
    document.addEventListener('visibilitychange', hidden); window.addEventListener('pagehide', stop);
    return () => { mounted.current = false; document.removeEventListener('visibilitychange', hidden); window.removeEventListener('pagehide', stop); stop(); };
  }, []);
  useEffect(() => () => release(), [client, controller.description?.boot_id]);
  async function start() {
    if (!parameters) return;
    stop(); setError(''); setListening('starting'); setFrames([[], []]); latest.current = undefined;
    const input = new MicrophoneAudio(snapshot => {
      if (capture.current !== input || !mounted.current) return;
      latest.current = snapshot; const epoch = snapshot.live[0]?.epoch;
      const changed = epoch !== historyEpoch.current; historyEpoch.current = epoch ?? -1;
      setFrames(previous => [1, 2].map(id => { const frame = snapshot.live.find(f => f.profile === id); return frame ? [...(changed ? [] : previous[id - 1]), frame].slice(-120) : previous[id - 1]; }));
      for (const p of sessions.current) { const frame = snapshot.published.find(f => f.profile === p.profile); if (frame) p.session.publish(frame); }
    }, problem => { if (capture.current === input) { stop(); setError(problem.message); } }, () => { if (capture.current === input) { release(); setListening('finished'); } });
    capture.current = input;
    try {
      await input.start(device, parameters, channel, fixture);
      if (capture.current !== input) return;
      setListening('active'); setCaptureInfo(input.inputInfo);
      if (!fixture) try { setDevices((await navigator.mediaDevices.enumerateDevices()).filter(item => item.kind === 'audioinput')); } catch { /* Device labels are optional. */ }
    } catch (problem) { if (capture.current === input) { stop(); setError((problem as Error).message); } }
  }
  async function publish() {
    if (!client || !controller.description || listening !== 'active' || sessions.current.length) return;
    const profiles = both && compare ? [1, 2] : [profile], snapshot = latest.current;
    if (!snapshot || profiles.some(id => !snapshot.published.some(f => f.profile === id))) { setError('Wait for the first complete publication hop.'); return; }
    if (!/^[a-zA-Z0-9_.-]{1,23}$/.test(name)) { setError('Source name needs 1–23 letters, digits, dots, underscores or hyphens.'); return; }
    setError('');
    const pending = profiles.map(id => ({ profile: id, session: new AudioSourceSession(client, controller.description!.boot_id,
      problem => { release(); if (mounted.current) setError(problem.message); }, profiles.length === 2 ? `${name}_${id}` : name) }));
    sessions.current = pending;
    try { for (const p of pending) await p.session.start(snapshot.published.find(f => f.profile === p.profile)!); if (sessions.current === pending) setForwarding(true); }
    catch (problem) { if (sessions.current === pending) { release(); setError((problem as Error).message); } }
  }
  async function exportResults() {
    try {
      const report = await (capture.current ?? lastCapture.current)?.export(); if (!report) throw new Error('Capture a comparison first.');
      const url = URL.createObjectURL(new Blob([JSON.stringify(report)], { type: 'application/json' }));
      const link = document.createElement('a'); link.href = url; link.download = 'audio-comparison.json'; link.click(); URL.revokeObjectURL(url);
    } catch (problem) { setError((problem as Error).message); }
  }
  const reset = () => { release(); capture.current?.reset(); setFrames([[], []]); latest.current = undefined; };
  if (!active) return listening === 'idle' ? null : <aside className="card" aria-label="Active music source">Audio {forwarding ? 'is sending to the controller' : 'is being analysed locally'}. <Link to="/audio">Audio controls</Link> <button onClick={stop}>Stop listening</button></aside>;
  return <div className="audio-demo"><div className="intro"><div><p className="eyebrow">04 / AUDIO PLAYGROUND</p><h1>Listen, compare, illuminate.</h1><p>Shared analysis for the Sense and your browser. Comparisons stay local until you choose to send them.</p></div></div>
    <section className="audio-master card"><h2>Capture and calibration</h2>
      <label htmlFor="audio-device">Audio input</label><select id="audio-device" value={device} disabled={listening !== 'idle' || !!fixture} onChange={e => setDevice(e.target.value)}><option value="">System default</option>{devices.filter(d => d.deviceId !== 'default').map((d, i) => <option key={d.deviceId} value={d.deviceId}>{d.label || `Input ${i + 1}`}</option>)}</select>
      <label>PCM fixture <input aria-label="PCM fixture" type="file" accept=".wav,.pcm" disabled={listening !== 'idle'} onChange={e => { const file = e.target.files?.[0]; if (file) void readAudioFixture(file).then(setFixture).catch(e => setError(e.message)); }} /></label>
      {fixture && <p>{fixture.name} · {fixture.rate} Hz · {fixture.channels.length} channels <button disabled={listening !== 'idle'} onClick={() => setFixture(undefined)}>Use microphone</button></p>}
      <label>Channel <select aria-label="Audio channel" disabled={listening !== 'idle'} value={channel} onChange={e => setChannel(Number(e.target.value))}><option value={-1}>Average all channels</option>{Array.from({ length: fixture?.channels.length ?? 2 }, (_, n) => <option key={n} value={n}>Channel {n + 1}</option>)}</select></label>
      <div className="audio-parameters">{manifest && parameters && Object.entries(manifest.parameters).map(([key, schema]) => <label key={key}>{labels[key as keyof AudioParameters]} {units[key as keyof AudioParameters] && <small>({units[key as keyof AudioParameters]})</small>}<input type="number" aria-label={labels[key as keyof AudioParameters]} min={schema.min} max={schema.max} step={schema.integer ? 1 : key === 'onset_threshold' ? 0.01 : 1} disabled={listening !== 'idle'} value={parameters[key as keyof AudioParameters]} onChange={e => setParameters({ ...parameters, [key]: Number(e.target.value) })} /></label>)}</div>
      <div className="audio-buttons"><button className="primary" disabled={!parameters || listening === 'active' || listening === 'starting'} onClick={() => void start()}>{listening === 'starting' ? 'Starting analyser…' : fixture ? 'Replay fixture silently' : 'Start listening'}</button><button disabled={listening === 'idle'} onClick={stop}>Stop listening</button><button disabled={listening !== 'active'} onClick={reset}>Reset both histories</button><button disabled={!frames[0].length} onClick={() => void exportResults()}>Export comparison</button></div>
      <p role="status">{listening === 'active' ? 'Listening · shared local audio analysis' : listening === 'finished' ? 'Fixture complete · comparison ready to export' : 'Audio capture off'}</p>
      <p className="helper">No audio is routed to speakers. Only calibrated features and onset events are forwarded. Changing calibration starts a new capture session.</p>
      {!!captureInfo.speech_processing && <p className="helper">Capture processing: {JSON.stringify(captureInfo.speech_processing)}. {captureInfo.unreported_device_latency ? 'The browser does not report device input latency; timing includes a conservative callback guard.' : ''}</p>}
    </section>
    <section className="card"><label>Algorithm <select aria-label="Audio algorithm" value={profile} onChange={e => { setProfile(Number(e.target.value)); reset(); }}>{(manifest?.profiles ?? [{ id: 1, label: 'Adaptive percussion v1' }]).map(p => <option key={p.id} value={p.id}>{p.label}</option>)}</select></label><label><input type="checkbox" checked={compare} onChange={e => setCompare(e.target.checked)} /> Compare both algorithms</label><p className="helper">{manifest?.revision} · same 48 kHz, 2,048-sample window and 1,024-sample hop. The level-rise baseline has its own profile ID.</p></section>
    <div className={`audio-comparison${compare ? ' comparing' : ''}`}>{(compare ? [1, 2] : [profile]).map(id => <AnalysisPanel key={id} profile={id} title={manifest?.profiles.find(p => p.id === id)?.label ?? 'Adaptive percussion v1'} frames={frames[id - 1]} />)}</div>
    <section className="audio-controller card"><h2>Send to your lights</h2><label>Gateway source name <input aria-label="Gateway source name" value={name} disabled={forwarding} onChange={e => setName(e.target.value)} /></label>
      {compare && <label><input type="checkbox" checked={both} disabled={forwarding} onChange={e => setBoth(e.target.checked)} /> Send both as {name}_1 and {name}_2</label>}
      <div className="audio-buttons"><button className="primary" disabled={listening !== 'active' || forwarding || !client} onClick={() => void publish()}>Send music to controller</button><button disabled={!forwarding} onClick={release}>Stop sending</button></div><p role="status">{forwarding ? 'Sending structured audio features' : 'Audio forwarding off'}</p>
      <p className="helper">Declare this gateway name in your scene’s Sources, then bind its alias to an INPUT. Profile and optional identity pins remain under the scene’s control. Fixture replay requires allow_synthetic. Stopping forwarding keeps your bindings and returns metrics to their fallback.</p>
    </section><LiveInputs active={active} />{error && <p className="notice" role="alert">{error}</p>}
  </div>;
}
