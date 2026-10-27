// Capture adapter, not a detector. All feature/onset decisions live in kaj-fi C.
export const HOP = 1024, RATE = 48000, RECORD = 160, PROFILES = 2, HISTORY_HOPS = 3000;
export const quantize = x => Number.isFinite(x) ? Math.max(-32768, Math.min(32767, Math.floor(x * 32768 + 0.5))) : 0;

/** Causal, phase-indexed 64-tap windowed-sinc resampler. Capture timestamps
 * follow the centre sample, including its 32-input-sample lookahead delay.
 * Tables/rings/hops are allocated before capture. A 48 kHz input bypasses FIR. */
export class PcmAdapter {
  constructor(rate, channel, hop) {
    if (!Number.isInteger(rate) || rate < 8000 || rate > 192000 || !Number.isInteger(channel) || channel < -1) throw new Error('Unsupported PCM capture geometry.');
    this.rate = rate; this.channel = channel; this.hop = hop;
    this.ring = new Float64Array(128); this.pcm = new Int16Array(HOP);
    this.table = new Float64Array(1024 * 64);
    const cutoff = 0.45 * Math.min(1, RATE / rate);
    for (let phase = 0; phase < 1024; phase++) {
      let sum = 0;
      for (let tap = 0; tap < 64; tap++) {
        const x = tap - 31 - phase / 1024;
        const sinc = x === 0 ? 2 * cutoff : Math.sin(2 * Math.PI * cutoff * x) / (Math.PI * x);
        const window = 0.5 + 0.5 * Math.cos(Math.PI * x / 32);
        const coefficient = Math.abs(x) >= 32 ? 0 : sinc * window;
        this.table[phase * 64 + tap] = coefficient; sum += coefficient;
      }
      for (let tap = 0; tap < 64; tap++) this.table[phase * 64 + tap] /= sum;
    }
    this.reset(0);
  }
  reset(startFrame) { this.ring.fill(0); this.used = 0; this.input = -1; this.output = 0; this.startFrame = startFrame; }
  emit(value, position, offsetUs, delayUs) {
    this.pcm[this.used++] = quantize(value);
    if (this.used === HOP) {
      this.used = 0;
      // Position is the captured sample centre, not callback arrival time.
      const endUs = offsetUs + (this.startFrame + position + 1) * 1e6 / this.rate - delayUs;
      this.hop(this.pcm, Math.max(1, Math.round(endUs)), this.output + 1);
    }
    this.output++;
  }
  push(channels, offsetUs, delayUs, start = 0, length = channels[0]?.length - start) {
    if (!channels.length || this.channel >= channels.length) return;
    for (let n = start; n < start + length; n++) {
      let value = 0;
      if (this.channel < 0) {
        for (let c = 0; c < channels.length; c++) value += Number.isFinite(channels[c][n]) ? channels[c][n] : 0;
        value /= channels.length;
      } else value = Number.isFinite(channels[this.channel][n]) ? channels[this.channel][n] : 0;
      this.input++; this.ring[this.input & 127] = value;
      if (this.rate === RATE) { this.emit(value, this.input, offsetUs, delayUs); continue; }
      let position = this.output * this.rate / RATE;
      while (Math.floor(position) + 32 <= this.input) {
        const centre = Math.floor(position), phase = Math.min(1023, Math.floor((position - centre) * 1024));
        let sum = 0;
        for (let tap = 0; tap < 64; tap++) {
          const index = centre + tap - 31;
          if (index >= 0) sum += this.ring[index & 127] * this.table[phase * 64 + tap];
        }
        this.emit(sum, position, offsetUs, delayUs); position = this.output * this.rate / RATE;
      }
    }
  }
}

/** Two independent C histories over one quantized hop. All callback storage is
 * bounded and preallocated. Layout: profile, ready-hop, capture-us, event-count,
 * six metrics, 64 bands, 65 bin edges, novelty, threshold, four (id,time,strength),
 * band-count, stream epoch, publication sequence, output sample end. */
export class AnalysisRunner {
  constructor(module, parameters, rate = RATE, channel = -1) {
    this.module = module; this.parameters = parameters; this.epoch = 0;
    this.latest = new Float64Array(RECORD * PROFILES); this.published = new Float64Array(RECORD * PROFILES);
    this.history = new Float64Array(HISTORY_HOPS * RECORD * PROFILES);
    this.handles = []; this.pcmViews = []; this.outputs = []; this.edges = [];
    const p = parameters;
    for (const profile of [1, 2]) {
      const h = module._kf_host_new(profile, p.gain_db, p.floor_db, p.attack_ms, p.release_ms, p.band_count, p.onset_threshold, p.onset_refractory_ms);
      if (!h) { this.destroy(); throw new Error('Audio parameters exceed the shared core contract.'); }
      this.handles.push(h);
      this.pcmViews.push(module.HEAP16.subarray(module._kf_host_pcm(h) / 2, module._kf_host_pcm(h) / 2 + HOP));
      this.outputs.push(module.HEAPF32.subarray(module._kf_host_output(h) / 4, module._kf_host_output(h) / 4 + 73));
      this.edges.push(module.HEAPU16.subarray(module._kf_host_edges(h) / 2, module._kf_host_edges(h) / 2 + p.band_count + 1));
    }
    this.ids = new Float64Array(PROFILES); this.events = new Float64Array(PROFILES * 12); this.eventCount = new Uint8Array(PROFILES);
    this.adapter = new PcmAdapter(rate, channel, (pcm, endUs, sample) => this.hop(pcm, endUs, sample));
    this.reset(0);
  }
  destroy() { for (const h of this.handles) this.module._kf_host_destroy(h); this.handles.length = 0; }
  reset(frame) {
    this.epoch++; this.hops = 0; this.historyCount = 0; this.historyTruncated = false; this.lastEnd = 0;
    this.ids.fill(0); this.events.fill(0); this.eventCount.fill(0); this.latest.fill(0); this.published.fill(0);
    for (const h of this.handles) this.module._kf_host_reset(h);
    this.adapter.reset(frame); this.expectedFrame = frame; this.hadInput = false;
  }
  process(channels, frame, offsetUs, delayUs, start = 0, length = channels[0]?.length - start) {
    if (!channels.length || !length || this.adapter.channel >= channels.length) { this.hadInput = false; return; }
    if (!this.hadInput || frame !== this.expectedFrame) this.reset(frame);
    this.hadInput = true; this.expectedFrame = frame + length;
    this.adapter.push(channels, offsetUs, delayUs, start, length);
  }
  hop(pcm, endUs, sample) {
    if (this.lastEnd && (endUs <= this.lastEnd || Math.abs(endUs - this.lastEnd - HOP * 1e6 / RATE) > 2)) {
      this.reset(this.expectedFrame); return;
    }
    this.lastEnd = endUs;
    let ready = true;
    for (let n = 0; n < PROFILES; n++) {
      this.pcmViews[n].set(pcm);
      if (this.module._kf_host_push(this.handles[n], endUs) !== 1) ready = false;
    }
    if (!ready) return;
    this.hops++;
    for (let n = 0; n < PROFILES; n++) {
      const out = this.outputs[n], base = n * RECORD, ebase = n * 12;
      let count = 0;
      for (let e = 0; e < this.eventCount[n]; e++) {
        const old = ebase + e * 3;
        if (endUs - this.events[old + 1] <= 350000) {
          const slot = ebase + count++ * 3;
          this.events[slot] = this.events[old]; this.events[slot + 1] = this.events[old + 1]; this.events[slot + 2] = this.events[old + 2];
        }
      }
      if (out[72] > 0) {
        if (count === 4) { this.events.copyWithin(ebase, ebase + 3, ebase + 12); count--; }
        const slot = ebase + count++ * 3;
        this.events[slot] = ++this.ids[n]; this.events[slot + 1] = endUs; this.events[slot + 2] = out[72];
      }
      this.eventCount[n] = count;
      this.latest[base] = n + 1; this.latest[base + 1] = this.hops; this.latest[base + 2] = endUs; this.latest[base + 3] = count;
      for (let i = 0; i < 70; i++) this.latest[base + 4 + i] = out[i];
      for (let i = 0; i <= this.parameters.band_count; i++) this.latest[base + 74 + i] = this.edges[n][i];
      this.latest[base + 139] = out[70]; this.latest[base + 140] = out[71];
      for (let i = 0; i < count * 3; i++) this.latest[base + 141 + i] = this.events[ebase + i];
      this.latest[base + 153] = this.parameters.band_count; this.latest[base + 154] = this.epoch;
      this.latest[base + 155] = Math.floor(this.hops / 5); this.latest[base + 156] = sample;
    }
    if (this.hops % 5 === 0) this.published.set(this.latest);
    if (this.historyCount < HISTORY_HOPS) {
      this.history.set(this.latest, this.historyCount++ * RECORD * PROFILES);
    } else this.historyTruncated = true;
  }
}
