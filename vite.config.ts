import { defineConfig } from 'vitest/config';
import react from '@vitejs/plugin-react';
import { cpSync, existsSync, mkdirSync, readFileSync, rmSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { COMPILER_FILES } from './web/src/compiler-files.mjs';

const root = fileURLToPath(new URL('.', import.meta.url));
const staticAssets = resolve(root, 'build/editor-assets');

// Package the canonical compiler; Vite bundles the language frontends in the worker.
rmSync(resolve(staticAssets, 'python'), { recursive: true, force: true });
mkdirSync(resolve(staticAssets, 'python/lightvm'), { recursive: true });
// Remove generated examples from both the previous and current layouts.
rmSync(resolve(staticAssets, 'samples'), { recursive: true, force: true });
rmSync(resolve(staticAssets, 'examples'), { recursive: true, force: true });
rmSync(resolve(staticAssets, 'compiler-worker.mjs'), { force: true });
for (const name of COMPILER_FILES) {
  cpSync(resolve(root, 'lightvm', name), resolve(staticAssets, 'python/lightvm', name));
}
cpSync(resolve(root, 'examples/animations'), resolve(staticAssets, 'examples/animations'), { recursive: true });
cpSync(resolve(root, 'examples/transitions'), resolve(staticAssets, 'examples/transitions'), { recursive: true });
export default defineConfig(({ mode }) => {
  if (mode !== 'test') {
    const audioBuild = resolve(root, 'build/audio');
    const required = ['revision.txt', 'profiles.json', 'kaj-audio.mjs', 'kaj-audio.wasm'];
    if (required.some(name => !existsSync(resolve(audioBuild, name)))) throw new Error('Build the shared analyser first: python3 tools/fetch_audio_core.py, then npm run audio:build with emcc on PATH.');
    if (readFileSync(resolve(audioBuild, 'revision.txt'), 'utf8').trim() !== readFileSync(resolve(root, 'kaj-audio.rev'), 'utf8').trim()) throw new Error('Rebuild the pinned audio WASM assets with npm run audio:build.');
    rmSync(resolve(staticAssets, 'audio'), { recursive: true, force: true });
    cpSync(audioBuild, resolve(staticAssets, 'audio'), { recursive: true });
    cpSync(resolve(root, 'web/audio'), resolve(staticAssets, 'audio'), { recursive: true });
  }
  return {
    root: resolve(root, 'web'), base: './', publicDir: staticAssets,
    plugins: [react()],
    build: { outDir: resolve(root, 'build/web'), emptyOutDir: true },
    test: {
      environment: 'jsdom', include: ['src/**/*.test.{ts,tsx}'],
      setupFiles: [resolve(root, 'web/src/test-setup.ts')], restoreMocks: true,
    },
  };
});
