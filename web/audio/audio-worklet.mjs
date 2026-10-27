import createModule from './kaj-audio.mjs';
import { AnalysisRunner, RECORD, PROFILES } from './pcm-adapter.mjs';

class KajAudioProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super(); this.stopped = false; this.runner = null;
    const config = options.processorOptions;
    this.offsetUs = config.offsetUs; this.delayUs = config.delayUs;
    this.fixture = null; this.finished = false; this.finishedMessage = { type: 'ended' };
    this.channelCount = 0; this.channelError = { type: 'error', message: 'The selected channel is not present in this audio input.' };
    this.packet = new Float64Array(RECORD * PROFILES * 2); this.transfers = [this.packet.buffer];
    this.message = { type: 'frame', buffer: this.packet.buffer }; this.lastHop = 0;
    this.port.onmessage = event => {
      const data = event.data;
      if (data instanceof ArrayBuffer) {
        this.packet = new Float64Array(data); this.transfers[0] = data; this.message.buffer = data;
      } else if (data.type === 'reset') {
        this.offsetUs = data.offsetUs; this.delayUs = data.delayUs;
        this.runner?.reset(currentFrame); this.lastHop = 0;
      } else if (data.type === 'fixture') {
        this.fixture = data; this.finished = false;
      } else if (data.type === 'export') {
        const r = this.runner;
        if (r) {
          const history = r.history.slice(0, r.historyCount * RECORD * PROFILES);
          this.port.postMessage({ type: 'export', id: data.id, epoch: r.epoch, truncated: r.historyTruncated, buffer: history.buffer }, [history.buffer]);
        }
      } else if (data.type === 'stop') { this.stopped = true; this.runner?.destroy(); }
    };
    // Main thread compiled the module. Instantiation, FFT tables, FIR tables and
    // all history/transfer storage are ready before the source is connected.
    createModule({ instantiateWasm(imports, receive) { receive(new WebAssembly.Instance(config.module, imports), config.module); } })
      .then(module => {
        if (this.stopped) return;
        this.runner = new AnalysisRunner(module, config.parameters, sampleRate, config.channel);
        this.port.postMessage({ type: 'ready' });
      }).catch(error => this.port.postMessage({ type: 'error', message: String(error) }));
  }
  process(inputs) {
    if (this.stopped) return false;
    const r = this.runner; if (!r) return true;
    if (this.finished) return true;
    const channels = inputs[0], length = channels[0]?.length ?? 0;
    if (channels.length) {
      if (r.adapter.channel >= channels.length) { this.port.postMessage(this.channelError); this.stopped = true; return false; }
      if (this.channelCount && channels.length !== this.channelCount) r.reset(currentFrame);
      this.channelCount = channels.length;
    }
    if (this.fixture) {
      const start = Math.max(0, this.fixture.start - currentFrame);
      const end = Math.min(length, this.fixture.end - currentFrame);
      if (end > start) r.process(channels, currentFrame + start, this.offsetUs, this.delayUs, start, end - start);
    } else r.process(channels, currentFrame, this.offsetUs, this.delayUs);
    // Only one transferable mailbox can be in flight. Continue DSP/history
    // while it is held by a stalled main thread; replace metrics, retain TTL events.
    if (this.packet && r.hops && r.hops !== this.lastHop) {
      this.lastHop = r.hops; this.packet.set(r.latest); this.packet.set(r.published, RECORD * PROFILES);
      this.port.postMessage(this.message, this.transfers); this.packet = null;
    }
    if (this.fixture && currentFrame + length >= this.fixture.end) {
      this.finished = true; this.port.postMessage(this.finishedMessage);
    }
    return true;
  }
}
registerProcessor('kaj-audio', KajAudioProcessor);
