import { defineConfig } from 'vitest/config';
import react from '@vitejs/plugin-react';
import { cpSync, mkdirSync, rmSync } from 'node:fs';
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
// These are served alongside the app: a lesson never needs a public CDN.
mkdirSync(resolve(staticAssets, 'vendor/pyodide'), { recursive: true });
for (const name of ['pyodide.mjs', 'pyodide.asm.mjs', 'pyodide.asm.wasm', 'python_stdlib.zip', 'pyodide-lock.json']) {
  cpSync(resolve(root, 'node_modules/pyodide', name), resolve(staticAssets, 'vendor/pyodide', name));
}
cpSync(resolve(root, 'node_modules/codemirror'), resolve(staticAssets, 'vendor/codemirror'), { recursive: true });
cpSync(resolve(root, 'THIRD_PARTY_NOTICES.md'), resolve(staticAssets, 'THIRD_PARTY_NOTICES.md'));
cpSync(resolve(root, 'licenses'), resolve(staticAssets, 'licenses'), { recursive: true });

export default defineConfig({
  root: resolve(root, 'web'), base: './', publicDir: staticAssets,
  plugins: [react()],
  build: { outDir: resolve(root, 'build/web'), emptyOutDir: true },
  test: {
    environment: 'jsdom', include: ['src/**/*.test.{ts,tsx}'],
    setupFiles: [resolve(root, 'web/src/test-setup.ts')], restoreMocks: true,
  },
});
