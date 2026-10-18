import fixtures from '../../tests/portable_fixtures.h?raw';
import { webcrypto } from 'node:crypto';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { bytesFor, compositionLayers, emptyLibrary, importLibrary, LIBRARY_KEY, parseLibrary, readLibrary, saveAnimation, writeLibrary } from './library';
import type { Compilation } from './types';

beforeEach(() => vi.stubGlobal('crypto', webcrypto));
afterEach(() => { localStorage.clear(); vi.unstubAllGlobals(); vi.restoreAllMocks(); });
const fixture = fixtures.split('library_lbc[] = {')[1].split('};')[0];
const bytecode = Uint8Array.from(fixture.match(/0x[0-9a-f]{2}/g)!, value => parseInt(value.slice(2),16));
const header = new DataView(bytecode.buffer);
const manifest = JSON.parse(new TextDecoder().decode(bytecode.subarray(bytecode.length-header.getUint32(76,true))));
const compiled: Compilation = { id: 1, source: '# editable source', bytecode, diagnostics: [], manifest };

describe('editable library and pinned presets', () => {
  it('keeps transition source and bytecode in the library without accepting it as a composition layer', async () => {
    const raw = fixtures.split('transition_lbc[] = {')[1].split('};')[0];
    const bytes = Uint8Array.from(raw.match(/0x[0-9a-f]{2}/g)!.map(value => parseInt(value, 16)));
    const size = new DataView(bytes.buffer).getUint32(76, true);
    const metadata = JSON.parse(new TextDecoder().decode(bytes.subarray(bytes.length-size)));
    const transition = { ...compiled, bytecode: bytes, manifest: metadata, source: '# editable transition' };
    const { library, animation } = await saveAnimation(emptyLibrary(), transition, 'crossfade.py', 'Crossfade', {});
    const imported = await importLibrary(emptyLibrary(), JSON.stringify(library));
    expect(imported.animations[0].manifest.profile).toBe('transition');
    expect(imported.animations[0].source).toBe('# editable transition');
    expect(() => compositionLayers(imported, [{ id: 'bad', animation: animation.id, settings: {}, opacity: 255, blend: 'over' }])).toThrow(/cannot be a composition layer/);
  });
  it('round trips source, compiled code and independent instance settings across library reloads', async () => {
    const first = await saveAnimation(emptyLibrary(), compiled, 'sweep.py', 'Sweep', { duration: 2 });
    first.library.presets.push({ id: 'blue', name: 'blue', layers: [{ id: 'eye', animation: first.animation.id, settings: { duration: 4, color: 255 }, opacity: 128, blend: 'screen' }] });
    const second = await saveAnimation(first.library, { ...compiled, source: '# updated', bytecode: compiled.bytecode }, 'sweep.py', 'Sweep', { duration: 1 });
    writeLibrary(second.library);
    const loaded = readLibrary().library;
    expect(loaded.animations.map(item => item.version)).toEqual([1, 2]);
    expect(loaded.animations[0].source).toBe('# editable source');
    expect(bytesFor(loaded.animations[0])).toEqual(compiled.bytecode);
    expect(compositionLayers(loaded, loaded.presets[0].layers)).toEqual([{ id: 'eye', program: 'a' + first.animation.digest.slice(0, 31), crc32: expect.any(Number), settings: { duration: 4, color: 255 }, opacity: 128, blend: 'screen' }]);
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

  it('opens and exports programs requiring newer releases while preserving preset references', async () => {
    function withManifest(next: typeof manifest): Compilation {
      const metadata = new TextEncoder().encode(JSON.stringify(next));
      const bytes = new Uint8Array(bytecode.length - header.getUint32(76,true) + metadata.length);
      bytes.set(bytecode.subarray(0, bytecode.length - header.getUint32(76,true)));
      bytes.set(metadata, bytes.length - metadata.length);
      new DataView(bytes.buffer).setUint32(76, metadata.length, true);
      return { ...compiled, manifest: next, bytecode: bytes };
    }
    const future = withManifest({ ...manifest, compiler_version: '1.10.0', requires: '1.2' });
    const { library, animation } = await saveAnimation(emptyLibrary(), future, 'sweep.py', 'Future', {});
    library.presets.push({ id: 'future', name: 'future', layers: [{ id: 'eye', animation: animation.id, settings: {}, opacity: 255, blend: 'over' }] });
    writeLibrary(library);
    expect(readLibrary().error).toBe('');
    expect(readLibrary().library.animations[0].manifest.requires).toBe('1.2');
    expect(await importLibrary(emptyLibrary(), JSON.stringify(library))).toEqual(library);
    // Pre-release experiments remain editable data, without an execution adapter.
    const obsolete = withManifest({ ...manifest, schema: 'lightvm.program/10' });
    const saved = await saveAnimation(emptyLibrary(), obsolete, 'old.py', 'Old source', {});
    const oldEnvelope = { ...saved.library, schema: 'lightvm.library/1' };
    expect(parseLibrary(JSON.stringify(oldEnvelope)).animations[0].source).toBe(compiled.source);
    expect(parseLibrary(JSON.stringify(oldEnvelope)).schema).toBe('lightvm.library');
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
