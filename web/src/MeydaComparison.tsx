import { AUDIO_SIGNALS, type AudioAnalysis, type EyeMappings, mapAudioToEye } from './audio-features';

const NOTES = ['C', 'C♯', 'D', 'D♯', 'E', 'F', 'F♯', 'G', 'G♯', 'A', 'A♯', 'B'];
export function MeydaComparison({ analysis, mappings, listening }: { analysis?: AudioAnalysis; mappings: EyeMappings; listening: boolean }) {
  const measurements = analysis?.measurements, reference = analysis?.reference;
  const comparable = Object.values(mappings).every(mapping => (AUDIO_SIGNALS as readonly string[]).includes(mapping.signal));
  const eye = reference && mapAudioToEye(reference, mappings);
  const barkMaximum = Math.max(1, ...analysis?.bark ?? []);
  return <section className="card meyda-comparison" aria-label="Meyda and Web Audio comparison">
    <h2>Compare the same sound</h2>
    <p className="muted">One microphone capture feeds both analyzers, with the same gain, noise floor, band boundaries and smoothing. The Eye above uses Meyda.</p>
    <table><thead><tr><th scope="col">Control signal</th><th scope="col">Web Audio</th><th scope="col">Meyda</th></tr></thead>
      <tbody>{AUDIO_SIGNALS.map(name => <tr key={name}><th scope="row">{name}</th><td><meter min="0" max="1" value={reference?.[name] ?? 0} aria-label={'Web Audio ' + name} /><output>{(reference?.[name] ?? 0).toFixed(2)}</output></td><td><meter min="0" max="1" value={analysis?.features[name] ?? 0} aria-label={'Meyda ' + name} /><output>{(analysis?.features[name] ?? 0).toFixed(2)}</output></td></tr>)}</tbody></table>
    {eye && comparable && <><p className="helper">Web Audio reference Eye · identical mappings</p><div className="audio-eye-strip reference-eye" role="img" aria-label={`Web Audio reference Eye: position ${eye.position.toFixed(2)}, width ${eye.width.toFixed(2)}, brightness ${eye.brightness.toFixed(2)}`}>
      <div className="audio-eye-brush" style={{ left: eye.position * (1 - eye.width) * 100 + '%', width: eye.width * 100 + '%', opacity: listening ? eye.brightness : 0, background: `linear-gradient(90deg, hsl(${eye.hue * 360} 100% 17%), hsl(${eye.hue * 360} 100% 50%), hsl(${eye.hue * 360} 100% 17%))` }} />
    </div></>}
    {!comparable && <p className="helper">The reference Eye is available when all mappings use the six shared signals. Flatness, roll-off and zero crossings are available on the Meyda page.</p>}
    <p className="helper">Both FFTs use Blackman windows; small window and sampling differences remain. Meyda’s centroid covers its full spectrum; the Web Audio adapter uses 40–12,000 Hz. Onset uses the same level-rise detector in both.</p>
    {analysis?.timings && <p className="helper">Main-thread feature work: Web Audio {analysis.timings.nativeMs.toFixed(2)} ms · Meyda {analysis.timings.meydaMs.toFixed(2)} ms. Web Audio’s FFT runs inside the browser.</p>}
    <h3>Meyda measurements</h3>
    <dl className="meyda-measurements">
      <div><dt>RMS · raw PCM</dt><dd>{(measurements?.rms ?? 0).toFixed(4)}</dd></div>
      <div><dt>Spectral centre</dt><dd>{(measurements?.centroidHz ?? 0).toFixed(0)} Hz</dd></div>
      <div><dt>99% roll-off</dt><dd>{(measurements?.rolloffHz ?? 0).toFixed(0)} Hz</dd></div>
      <div><dt>Flatness</dt><dd>{(measurements?.flatness ?? 0).toFixed(3)}</dd></div>
      <div><dt>Zero crossings / frame</dt><dd>{measurements?.crossings ?? 0}</dd></div>
      <div><dt>Loudness · Meyda units</dt><dd>{(measurements?.loudness ?? 0).toFixed(2)}</dd></div>
    </dl>
    <h3>Pitch-class energy</h3><p className="helper">Chroma folds pitches into twelve classes. These bars show relative strengths, without identifying a musical key.</p>
    <div className="meyda-chroma">{NOTES.map((note, index) => <div key={note}><div className="meyda-bar-track"><div style={{ height: ((analysis?.chroma?.[index] ?? 0) * 100) + '%' }} /></div><span>{note}</span><output>{(analysis?.chroma?.[index] ?? 0).toFixed(2)}</output></div>)}</div>
    <h3>24 Bark loudness bands</h3><p className="helper">Low frequencies on the left, high on the right. Bar heights are relative to the strongest current band.</p>
    <div className="meyda-bark" role="img" aria-label="24 relative Bark loudness bands">{Array.from({ length: 24 }, (_, index) => <div className="meyda-bar-track" key={index}><div style={{ height: ((analysis?.bark?.[index] ?? 0) / barkMaximum * 100) + '%' }} /></div>)}</div>
  </section>;
}
