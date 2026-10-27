#!/usr/bin/env node
// Build matching, editable course files from a checked block description.
import { readFileSync, writeFileSync, mkdirSync, mkdtempSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import { pathToFileURL, fileURLToPath } from 'node:url';
import { stripTypeScriptTypes } from 'node:module';
import { lessons } from './teaching_lessons.mjs';
const root = fileURLToPath(new URL('..', import.meta.url)), check = process.argv.includes('--check');
const temporary = mkdtempSync(join(root, 'build/teaching-sources-'));
function save(name, text) {
  const file = join(root, name); mkdirSync(join(file, '..'), { recursive: true });
  if (check) { if (readFileSync(file, 'utf8') !== text) throw new Error('Regenerate course file: ' + name); }
  else writeFileSync(file, text);
}
try {
  for (const name of ['source', 'blocks']) {
    writeFileSync(join(temporary, name + '.mjs'), stripTypeScriptTypes(readFileSync(join(root, `web/src/languages/${name}.ts`), 'utf8'), { mode: 'transform' }).replaceAll("'./source'", "'./source.mjs'"));
  }
  const { lowerBlocks } = await import(pathToFileURL(join(temporary, 'blocks.mjs')));
  const catalogue = [];
  for (const lesson of lessons) {
    const folder = 'examples/lessons/' + lesson.id;
    for (const version of ['starter', 'solution', 'extension', 'bug']) {
      const document = lesson.make(version); let serial = 0;
      const ids = block => { block.id = `${lesson.id}-${version}-${++serial}`; for (const input of Object.values(block.inputs ?? {})) if (input.block) ids(input.block); if (block.next) ids(block.next.block); };
      document.blocks.blocks.forEach(ids);
      const saved = JSON.stringify(document, null, 2) + '\n', lowered = lowerBlocks(saved);
      // Text lessons use the learner's names. Keep only imports that they read.
      let python = lowered.python.replace(/\b(?:v_|fn_)([A-Za-z_]\w*)/g, '$1').replace(/\binit_program\b/g, 'start').replace(/\brender_program\b/g, 'draw');
      const body = python.slice(python.indexOf('profile('));
      const needed = name => new RegExp('\\b' + name + '\\b').test(body);
      const imports = python.slice(0, python.indexOf('profile(')).trim().split('\n').flatMap(line => {
        const imported = /^from (\S+) import (.*)$/.exec(line);
        if (imported) { const names = imported[2].split(', ').filter(needed); return names.length ? [`from ${imported[1]} import ${names.join(', ')}`] : []; }
        return needed(line.replace('import ', '')) ? [line] : [];
      });
      python = [...imports, '', body].join('\n')
        .replace(/int\(pixels.count\)/g, 'pixels.count')
        .replace(/int\((-?\d+)(?:\.0)?\)/g, '$1')
        .replace(/int\(\(pixels.count - 1\.0\)\)/g, '(pixels.count - 1)')
        .replace(/@init\ndef start\(state, runtime\):\n    pass\n/g, '')
        .replace(/\n(?=@(?:init|render)|def )/g, '\n\n')
        .replace(/(@(?:init|render))\n\ndef /g, '$1\ndef ');
      if (!python.includes('@init')) python = python.replace(/, init\b/g, '');
      const note = version === 'bug' ? 'This version has a mistake. Find it and explain your repair.' : lesson.problem;
      save(folder + '/' + version + '.py', '# ' + note + '\n' + python);
      // Keep whole-number helpers for array counts and explicitly typed values.
      // Ordinary per-pixel loops use familiar JavaScript counting syntax.
      const counters = new Set([...lowered.javascript.matchAll(/for \((\w+) of range\(int\(pixels.count\)\)\)/g)].map(match => match[1]));
      // Course remainders all have positive lengths. The public wrap helper
      // keeps backwards motion clear without showing a compiler-generated helper.
      const javascript = lowered.javascript.replace(/^function _blocks_modulo[^\n]*\n/gm, '').replaceAll('_blocks_modulo(', 'wrap(')
        .replace(/for \((\w+) of range\(int\(pixels.count\)\)\)/g, 'for (let $1 = 0; $1 < pixels.count; $1++)')
        .replace(/export function init\(state, runtime\) \{\n  \/\/ Try adding a block here.\n\}\n/g, '')
        .replace(/^  let ([\w, ]+);\n/gm, (line, names) => { const remaining = names.split(', ').filter(name => !counters.has(name)); return remaining.length ? '  let ' + remaining.join(', ') + ';\n' : ''; });
      save(folder + '/' + version + '.js', '// ' + note + '\n' + javascript);
      save(folder + '/' + version + '.blocks.json', saved);
    }
    const { make, ...entry } = lesson; catalogue.push(entry);
    save(folder + '/README.md', `# ${lesson.title}\n\n${lesson.problem}\n\n${lesson.idea}\n\n1. **Predict:** ${lesson.predict}\n2. **Try:** Open a starter in Python, JavaScript or Blocks. Start with 25 lights.\n3. **Change:** ${lesson.task}\n4. **Check:** ${lesson.check}\n5. **Build on it:** ${lesson.extension}\n6. **Explain:** ${lesson.explain}\n\nEach language has four files: \`starter\`, \`solution\`, \`extension\` and \`bug\`.\nTry the task before opening the solution. The bug is deliberate. ${lesson.bug}\n\n[Back to the course](../README.md)\n`);
  }
  save('examples/lessons/catalogue.json', JSON.stringify(catalogue, null, 2) + '\n');
  save('examples/lessons/README.md', '# Learn with lights\n\nStart with 25 lights in the local preview. A controller is optional. Choose Python, JavaScript or Blocks and stay with it while learning an idea. The other versions let you compare how to say the same thing later.\n\nAllow about 30–45 minutes per lesson; take longer when a prediction needs discussion. Predict, run, change one thing, and explain what happened. Use 5 lights to trace a loop by hand. Try 25, 50 and 100 lights before finishing a project, or 12, 16 and 24 for a ring.\n\n' + lessons.map((l, i) => `${i + 1}. [${l.title}](${l.id}/README.md) — ${l.problem}`).join('\n') + '\n\nA frame is one finished picture. Drawing inside a loop does not show several separate pictures. Local names start fresh each call; remembered values survive into the next frame. There is no delay or sleep inside the drawing function.\n\nColours use red, green and blue from 0 to 255. Brightness examples use 0 to 1. Whole pixel positions run from 0 to count minus 1. The examples start dim so their changes are easy to compare. The preview shows a black background where nothing is painted; on a controller, another layer can show through that space.\n\nUse **Next frame** to choose how much time passes. Use **Step through the code** and **Watch a value** to inspect one statement at a time. Only a finished frame changes the displayed lights.\n\n## For teachers\n\nThe course files are editable source. Save a local source file before switching examples. Every lesson includes a deliberate bug and an extension, so assessment includes repairing and changing a program. A passing example is only the beginning.\n\n[Implementation and acceptance record](../../docs/design/teaching-implementation.md) · [Course review](../../docs/design/teaching-language-review-2026-10-27.md)\n');
  console.log(`Checked/generated ${lessons.length} lessons in three languages, each with four versions.`);
} finally { rmSync(temporary, { recursive: true, force: true }); }
