import { webcrypto } from 'node:crypto';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { bytesFor, deviceLayers, emptyLibrary, importLibrary, LIBRARY_KEY, parseLibrary, readLibrary, saveAnimation, writeLibrary } from './library';
import type { Compilation } from './types';

beforeEach(() => vi.stubGlobal('crypto', webcrypto));
afterEach(() => { localStorage.clear(); vi.unstubAllGlobals(); vi.restoreAllMocks(); });
const compiled: Compilation = { id: 1, source: '# editable source', bytecode: new Uint8Array([76, 66, 67, 49, 4, 0, 0, 0]), diagnostics: [],
  manifest: { abi: 4, profile: 'animation/1', parameters: [], settings: [{ name: 'duration', type: 'float' }, { name: 'color', type: 'rgb8' }] } };

describe('editable library and pinned presets', () => {
  it('round trips source, compiled code and independent instance settings across library reloads', async () => {
    const first = await saveAnimation(emptyLibrary(), compiled, 'sweep.py', 'Sweep', { duration: 2 });
    first.library.presets.push({ id: 'blue', name: 'blue', layers: [{ id: 'eye', animation: first.animation.id, settings: { duration: 4, color: 255 }, opacity: 128, blend: 'screen' }] });
    const second = await saveAnimation(first.library, { ...compiled, source: '# updated', bytecode: new Uint8Array([76, 66, 67, 49, 4, 1, 0, 0]) }, 'sweep.py', 'Sweep', { duration: 1 });
    writeLibrary(second.library);
    const loaded = readLibrary().library;
    expect(loaded.animations.map(item => item.version)).toEqual([1, 2]);
    expect(loaded.animations[0].source).toBe('# editable source');
    expect(bytesFor(loaded.animations[0])).toEqual(compiled.bytecode);
    expect(deviceLayers(loaded, loaded.presets[0].layers)).toEqual([{ id: 'eye', procedure: 'a' + first.animation.digest.slice(0, 31), crc32: expect.any(Number), settings: { duration: 4, color: 255 }, opacity: 128, blend: 'screen' }]);
    expect(loaded.animations[0].settings).toEqual({ duration: 2 });
    expect(await importLibrary(emptyLibrary(), JSON.stringify(loaded))).toEqual(loaded);
  });

  it('rejects broken references, invalid settings and corrupted bytecode during import', async () => {
    const { library, animation } = await saveAnimation(emptyLibrary(), compiled, 'sweep.py', 'Sweep', {});
    const text = JSON.stringify(library);
    expect(() => parseLibrary(text.replace('"settings":{}', '"settings":{"duration":true}'))).toThrow(/valid float/);
    library.presets.push({ id: 'bad', name: 'bad', layers: [{ id: 'eye', animation: 'missing', settings: {}, opacity: 255, blend: 'over' }] });
    expect(() => parseLibrary(JSON.stringify(library))).toThrow(/preset layer/);
    library.presets = [];
    animation.bytecode = btoa('corrupted bytes');
    await expect(importLibrary(emptyLibrary(), JSON.stringify(library))).rejects.toThrow(/checksum/);
  });

  it('reports quota failure and preserves unreadable data until explicit recovery', () => {
    localStorage.setItem(LIBRARY_KEY, '{broken');
    expect(readLibrary().error).toMatch(/could not be opened/);
    expect(localStorage.getItem(LIBRARY_KEY)).toBe('{broken');
    vi.spyOn(Storage.prototype, 'setItem').mockImplementation(() => { throw new Error('quota exceeded'); });
    expect(() => writeLibrary(emptyLibrary())).toThrow(/quota/);
    expect(localStorage.getItem(LIBRARY_KEY)).toBe('{broken');
  });
});
