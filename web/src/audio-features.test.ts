import { afterEach, describe, expect, it, vi } from 'vitest';
import { MicrophoneAudio, defaultParameters, validateParameters, readAudioFixture } from './audio-features';
import { manifest, parameters } from './audio-test-data';
afterEach(() => { vi.unstubAllGlobals(); vi.restoreAllMocks(); vi.useRealTimers(); });
it('derives parameters from the shared manifest and enforces its range contract', () => {
  expect(defaultParameters(manifest)).toEqual(parameters);
  expect(() => validateParameters({ ...parameters, onset_threshold: NaN }, manifest)).toThrow();
});
describe('AudioWorklet capture lifecycle', () => {
  function audio() {
    vi.stubGlobal('isSecureContext', true);
    vi.stubGlobal('fetch', vi.fn(async (url: URL) => ({ ok: true, json: async () => manifest, text: async () => 'revision', arrayBuffer: async () => new ArrayBuffer(8) })));
    vi.spyOn(WebAssembly, 'compile').mockResolvedValue({} as WebAssembly.Module);
    const stop = vi.fn(), connect = vi.fn(), disconnect = vi.fn();
    const context = { state: 'running', currentTime: 0.01, sampleRate: 48000, baseLatency: 0.005, outputLatency: 0.024,
      getOutputTimestamp: () => ({ contextTime: 0.01, performanceTime: 100 }), resume: vi.fn(async () => {}), close: vi.fn(async () => { context.state = 'closed'; }),
      audioWorklet: { addModule: vi.fn(async () => {}) }, createMediaStreamSource: () => ({ connect, disconnect }) };
    vi.stubGlobal('AudioContext', class { constructor() { return context; } });
    let worklet: any;
    vi.stubGlobal('AudioWorkletNode', class {
      port = { onmessage: (_: any) => {}, postMessage: vi.fn((data: any) => { if (data.type === 'export') queueMicrotask(() => this.port.onmessage({ data: { type: 'export', buffer: new ArrayBuffer(0), epoch: 1, truncated: false } })); }) };
      disconnect = vi.fn(); onprocessorerror?: () => void;
      constructor(_context: unknown, _name: string, public options: any) { worklet = this; queueMicrotask(() => this.port.onmessage({ data: { type: 'ready' } })); }
    });
    const track = { stop, addEventListener: vi.fn(), getSettings: () => ({ echoCancellation: false, noiseSuppression: false, autoGainControl: false, latency: 0.01, sampleRate: 48000, channelCount: 2 }) };
    const stream = { getTracks: () => [track], getAudioTracks: () => [track] } as unknown as MediaStream;
    return { context, stream, stop, connect, disconnect, worklet: () => worklet };
  }
  it('disables speech processing, uses an input-only worklet and releases capture', async () => {
    const mock = audio(), getUserMedia = vi.fn(async () => mock.stream); vi.stubGlobal('navigator', { mediaDevices: { getUserMedia } });
    const capture = new MicrophoneAudio(vi.fn(), vi.fn()); await capture.start('line-in', parameters);
    expect(getUserMedia).toHaveBeenCalledWith({ audio: { deviceId: { exact: 'line-in' }, echoCancellation: false, noiseSuppression: false, autoGainControl: false }, video: false });
    expect(mock.worklet().options.numberOfOutputs).toBe(0); expect(mock.connect).toHaveBeenCalledExactlyOnceWith(mock.worklet());
    expect(capture.inputInfo.reported_capture_latency_s).toBe(0.01);
    await capture.stop(); expect(mock.stop).toHaveBeenCalled(); expect(mock.context.close).toHaveBeenCalled(); expect(mock.disconnect).toHaveBeenCalled();
  });
  it('stops a stream granted after cancellation and never connects it', async () => {
    const mock = audio(); let grant!: (stream: MediaStream) => void;
    vi.stubGlobal('navigator', { mediaDevices: { getUserMedia: () => new Promise<MediaStream>(resolve => { grant = resolve; }) } });
    const capture = new MicrophoneAudio(vi.fn(), vi.fn()); const pending = capture.start('', parameters); await capture.stop(); grant(mock.stream); await pending;
    expect(mock.stop).toHaveBeenCalled(); expect(mock.connect).not.toHaveBeenCalled();
  });
  it('closes after permission denial and reports processor failures', async () => {
    const mock = audio(); vi.stubGlobal('navigator', { mediaDevices: { getUserMedia: vi.fn().mockRejectedValue(new Error('Permission denied')) } });
    await expect(new MicrophoneAudio(vi.fn(), vi.fn()).start('', parameters)).rejects.toThrow('Permission denied');
    expect(mock.context.close).toHaveBeenCalled();
    const second = audio(), failed = vi.fn(); vi.stubGlobal('navigator', { mediaDevices: { getUserMedia: async () => second.stream } });
    const capture = new MicrophoneAudio(vi.fn(), failed); await capture.start('', parameters); second.worklet().onprocessorerror();
    expect(failed).toHaveBeenCalledWith(expect.objectContaining({ message: expect.stringContaining('processor failed') })); await capture.stop();
  });
  it('resets on a stepped sample clock, ignores small jitter, and stops on suspension', async () => {
    vi.useFakeTimers(); const mock = audio(), failed = vi.fn();
    vi.stubGlobal('navigator', { mediaDevices: { getUserMedia: async () => mock.stream } });
    const stamp = { contextTime: 0.01, performanceTime: 100 };
    mock.context.getOutputTimestamp = () => stamp;
    const capture = new MicrophoneAudio(vi.fn(), failed), starting = capture.start('', parameters);
    await vi.advanceTimersByTimeAsync(200); await starting;
    const resets = () => mock.worklet().port.postMessage.mock.calls.filter(([message]: any[]) => message.type === 'reset');
    const initial = resets().length;
    stamp.performanceTime += 10; await vi.advanceTimersByTimeAsync(1000); expect(resets()).toHaveLength(initial);
    stamp.performanceTime += 40; await vi.advanceTimersByTimeAsync(1000); expect(resets()).toHaveLength(initial + 1);
    mock.context.state = 'suspended'; (mock.context as unknown as { onstatechange: () => void }).onstatechange();
    expect(failed).toHaveBeenCalledWith(expect.objectContaining({ message: expect.stringContaining('suspended') }));
    await capture.stop(); expect(mock.context.close).toHaveBeenCalledTimes(1); expect(mock.stop).toHaveBeenCalled();
  });
  it('rejects a missing selected channel and releases the granted microphone', async () => {
    const mock = audio(); vi.stubGlobal('navigator', { mediaDevices: { getUserMedia: async () => mock.stream } });
    await expect(new MicrophoneAudio(vi.fn(), vi.fn()).start('', parameters, 2)).rejects.toThrow('selected channel');
    expect(mock.stop).toHaveBeenCalled(); expect(mock.connect).not.toHaveBeenCalled();
  });
});
it('rejects unsupported file inputs before audio capture', async () => {
  const file = { size: 1, name: 'file.mp3', arrayBuffer: async () => new Uint8Array([1]).buffer } as File;
  await expect(readAudioFixture(file)).rejects.toThrow('Choose PCM16');
});
