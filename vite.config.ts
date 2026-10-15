import { defineConfig } from 'vitest/config';
import react from '@vitejs/plugin-react';
import { cpSync, mkdirSync, rmSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('.', import.meta.url));
const staticAssets = resolve(root, 'build/editor-assets');

// Package the canonical compiler and leave the worker's remote imports intact.
mkdirSync(resolve(staticAssets, 'python/lightvm'), { recursive: true });
// Remove generated examples from both the previous and current layouts.
rmSync(resolve(staticAssets, 'samples'), { recursive: true, force: true });
rmSync(resolve(staticAssets, 'examples'), { recursive: true, force: true });
cpSync(resolve(root, 'web/compiler-worker.mjs'), resolve(staticAssets, 'compiler-worker.mjs'));
for (const name of ['__init__.py', 'compiler.py', 'bytecode.py', 'portable.py', 'easing.py']) {
  cpSync(resolve(root, 'lightvm', name), resolve(staticAssets, 'python/lightvm', name));
}
cpSync(resolve(root, 'examples/animations'), resolve(staticAssets, 'examples/animations'), { recursive: true });

export default defineConfig({
  root: resolve(root, 'web'), base: './', publicDir: staticAssets,
  plugins: [react()],
  build: { outDir: resolve(root, 'build/web'), emptyOutDir: true },
  test: {
    environment: 'jsdom', include: ['src/**/*.test.{ts,tsx}'],
    setupFiles: [resolve(root, 'web/src/test-setup.ts')], restoreMocks: true,
  },
});
