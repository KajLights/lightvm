// @vitest-environment node
import { execFileSync } from 'node:child_process';
import { cpSync, mkdtempSync, mkdirSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { expect, it } from 'vitest';
import { COMPILER_FILES } from './compiler-files.mjs';

it('packages the complete compiler import closure for the browser worker', () => {
  const root = fileURLToPath(new URL('../..', import.meta.url));
  const directory = mkdtempSync(join(tmpdir(), 'lightvm-compiler-assets-'));
  try {
    mkdirSync(join(directory, 'lightvm'));
    for (const name of COMPILER_FILES) cpSync(join(root, 'build/editor-assets/python/lightvm', name), join(directory, 'lightvm', name));
    const script = `import sys\nsys.path.insert(0,sys.argv[1])\nfrom lightvm.compiler import compile_source\nassert compile_source('from lightvm import profile,render,fixed,rgb\\nprofile("animation")\\n@render\\ndef draw(s,r,p):p.fill(rgb(int(fixed(1.5)),0,0))').success\nprint('ok')`;
    expect(execFileSync(process.env.LIGHTVM_PYTHON || 'python3', ['-I', '-c', script, directory], { encoding: 'utf8', cwd: directory }).trim()).toBe('ok');
  } finally { rmSync(directory, { recursive: true, force: true }); }
});
