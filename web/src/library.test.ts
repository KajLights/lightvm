import fixtures from '../../tests/portable_fixtures.h?raw';
import { webcrypto } from 'node:crypto';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { bytesFor, deviceLayers, emptyLibrary, importLibrary, LIBRARY_KEY, parseLibrary, readLibrary, saveAnimation, writeLibrary } from './library';
import type { Compilation } from './types';

beforeEach(() => vi.stubGlobal('crypto', webcrypto));
afterEach(() => { localStorage.clear(); vi.unstubAllGlobals(); vi.restoreAllMocks(); });
const fixture = fixtures.split('library_lbc[] = {')[1].split('};')[0];
const bytecode = Uint8Array.from(fixture.match(/0x[0-9a-f]{2}/g)!, value => parseInt(value.slice(2),16));
const header = new DataView(bytecode.buffer);
const manifest = JSON.parse(new TextDecoder().decode(bytecode.subarray(bytecode.length-header.getUint32(76,true))));
const compiled: Compilation = { id: 1, source: '# editable source', bytecode, diagnostics: [], manifest };

describe('editable library and pinned presets', () => {
  it('round trips source, compiled code and independent instance settings across library reloads', async () => {
    const first = await saveAnimation(emptyLibrary(), compiled, 'sweep.py', 'Sweep', { duration: 2 });
    first.library.presets.push({ id: 'blue', name: 'blue', layers: [{ id: 'eye', animation: first.animation.id, settings: { duration: 4, color: 255 }, opacity: 128, blend: 'screen' }] });
    const second = await saveAnimation(first.library, { ...compiled, source: '# updated', bytecode: compiled.bytecode }, 'sweep.py', 'Sweep', { duration: 1 });
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
    await expect(importLibrary(emptyLibrary(), JSON.stringify(library))).rejects.toThrow(/Unsupported compiled/);
  });

  it('rejects obsolete schemas, profile bindings and mismatched embedded metadata without erasing saved data', async () => {
    const { library } = await saveAnimation(emptyLibrary(), compiled, 'color.py', 'Color', {});
    for (const schema of ['lightvm.program/3', 'lightvm.program/10']) {
      const old = structuredClone(library); old.animations[0].manifest.schema = schema;
      localStorage.setItem(LIBRARY_KEY, JSON.stringify(old));
      expect(() => parseLibrary(JSON.stringify(old))).toThrow(/Recompile/);
      expect(readLibrary().error).toBeTruthy();
      expect(readLibrary().backup).toBe(JSON.stringify(old));
      expect(localStorage.getItem(LIBRARY_KEY)).toBe(JSON.stringify(old));
    }
    const patched = new Uint8Array(compiled.bytecode); new DataView(patched.buffer).setUint32(72,1,true);
    await expect(saveAnimation(emptyLibrary(), {...compiled,bytecode:patched}, 'x.py','X',{})).rejects.toThrow(/Recompile/);
    await expect(saveAnimation(emptyLibrary(), {...compiled,manifest:{...manifest,settings:[]}}, 'x.py','X',{})).rejects.toThrow(/Recompile/);
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
