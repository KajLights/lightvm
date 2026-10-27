#!/usr/bin/env node
// Lower every published lesson before handing it to the common native checks.
import { readFileSync, writeFileSync, mkdirSync, mkdtempSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import { pathToFileURL, fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';
import { stripTypeScriptTypes } from 'node:module';
const root = fileURLToPath(new URL('..', import.meta.url));
mkdirSync(join(root, 'build'), { recursive: true });
const temporary = mkdtempSync(join(root, 'build/teaching-check-'));
try {
  for (const name of ['source', 'javascript', 'blocks']) writeFileSync(join(temporary, name + '.mjs'), stripTypeScriptTypes(readFileSync(join(root, `web/src/languages/${name}.ts`), 'utf8'), { mode: 'transform' }).replaceAll("'./source'", "'./source.mjs'"));
  const { lowerJavaScript } = await import(pathToFileURL(join(temporary, 'javascript.mjs')));
  const { lowerBlocks } = await import(pathToFileURL(join(temporary, 'blocks.mjs')));
  const catalogue = JSON.parse(readFileSync(join(root, 'examples/lessons/catalogue.json'), 'utf8')), cases = [];
  for (const lesson of catalogue) for (const variant of ['starter', 'solution', 'extension', 'bug']) for (const language of ['python', 'javascript', 'blocks']) {
    const extension = { python: 'py', javascript: 'js', blocks: 'blocks.json' }[language];
    const filename = `examples/lessons/${lesson.id}/${variant}.${extension}`;
    const source = readFileSync(join(root, filename), 'utf8');
    try {
      const lowered = language === 'python' ? { python: source } : language === 'javascript' ? lowerJavaScript(source) : lowerBlocks(source);
      cases.push({ lesson: lesson.id, variant, language, filename, source: lowered.python });
    } catch (error) { cases.push({ lesson: lesson.id, variant, language, filename, error: error.message }); }
  }
  try { process.stdout.write(execFileSync(process.env.LIGHTVM_PYTHON || 'python3', ['tools/check_teaching.py', ...process.argv.slice(2)], { cwd: root, input: JSON.stringify(cases), encoding: 'utf8', maxBuffer: 16 * 1024 * 1024 })); }
  catch (error) { process.stdout.write(error.stdout ?? ''); process.exitCode = 1; }
} finally { rmSync(temporary, { recursive: true, force: true }); }
