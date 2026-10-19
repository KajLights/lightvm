import { useEffect, useRef, useState } from 'react';
import { useAppSelector } from './store';
import { useRuntime } from './runtime';
import { EXAMPLES, exampleForSource } from './examples';
import { DevelopmentWorkflow } from './Workflow';
import type { Diagnostic } from './types';

function Editor() {
  const { compiler, controllers } = useRuntime(), editor = useAppSelector(state => state.editor);
  const textarea = useRef<HTMLTextAreaElement>(null), cm = useRef<any>(null), marks = useRef<any[]>([]);
  const [cursor, setCursor] = useState('Ln 1, Col 1');
  const example = exampleForSource(editor.source);
  useEffect(() => {
    compiler.start();
    if (!window.CodeMirror || !textarea.current) return;
    const instance = window.CodeMirror.fromTextArea(textarea.current, {
      mode: { name: 'python', version: 3 }, lineNumbers: true, indentUnit: 4, tabSize: 4,
      indentWithTabs: false, autoCloseBrackets: true, matchBrackets: true,
      gutters: ['CodeMirror-linenumbers', 'error-gutter'],
      extraKeys: {
        Tab: (editor: any) => editor.somethingSelected() ? editor.indentSelection('add') : editor.replaceSelection('    ', 'end'),
        'Shift-Tab': (editor: any) => editor.indentSelection('subtract'),
        'Ctrl-Enter': () => document.getElementById('upload')?.click(),
        'Cmd-Enter': () => document.getElementById('upload')?.click(),
      },
    });
    cm.current = instance; instance.getInputField().setAttribute('aria-label', 'Python source');
    instance.on('change', () => compiler.edit(instance.getValue()));
    instance.on('cursorActivity', () => { const pos = instance.getCursor(); setCursor('Ln ' + (pos.line + 1) + ', Col ' + (pos.ch + 1)); });
    return () => { marks.current.forEach(mark => mark.clear()); marks.current = []; cm.current = null; instance.toTextArea(); };
  }, [compiler]);
  useEffect(() => { if (cm.current && cm.current.getValue() !== editor.source) cm.current.setValue(editor.source); }, [editor.source]);
  useEffect(() => {
    const instance = cm.current;
    if (!instance) return;
    marks.current.forEach(mark => mark.clear()); marks.current = []; instance.clearGutter('error-gutter');
    for (const item of editor.diagnostics) {
      const line = Math.max(0, Math.min(item.line - 1, instance.lineCount() - 1));
      const content = instance.getLine(line), column = Math.max(0, Math.min(item.column - 1, content.length - 1));
      if (content.length) marks.current.push(instance.markText({ line, ch: column }, { line, ch: column + 1 }, { className: 'code-error', title: item.message }));
      const marker = document.createElement('span'); marker.className = 'error-marker'; marker.textContent = '●'; marker.title = item.message;
      instance.setGutterMarker(line, 'error-gutter', marker);
    }
  }, [editor.diagnostics]);
  function loadExample(name: string) {
    const selected = EXAMPLES.find(item => item.id === name);
    if (selected) compiler.edit(selected.source, selected.filename);
  }
  function focusDiagnostic(item: Diagnostic) {
    if (cm.current) { cm.current.focus(); cm.current.setCursor({ line: item.line - 1, ch: item.column - 1 }); cm.current.scrollIntoView({ line: item.line - 1, ch: item.column - 1 }, 60); }
    else textarea.current?.focus();
  }
  return <section className="editor-panel" aria-label="Python source editor">
    <div className="editor-toolbar"><div className="file-label"><span className="python-dot" /><span id="filename">{editor.filename}</span></div><div className="toolbar-actions"><label className="sr-only" htmlFor="example">Load example</label><select id="example" value={example?.id ?? ''} onChange={event => loadExample(event.target.value)}><option value="">Examples…</option>{EXAMPLES.map(item => <option key={item.id} value={item.id}>{item.label}</option>)}</select></div></div>
    {example && <p id="example-description" className="helper">{example.description}</p>}
    <label className="sr-only" htmlFor="source">Python source</label><textarea ref={textarea} id="source" value={editor.source} spellCheck={false} autoCapitalize="off" autoComplete="off" onChange={event => compiler.edit(event.target.value)} />

    <div className="editor-footer"><span id="cursor-position">{cursor}</span><span>Python subset · Portable bytecode</span><span id="draft-state">Session draft</span></div>
    <div className="diagnostics-header"><h2>Code checks</h2><span id="check-state">{editor.compiler === 'error' ? 'Compiler unavailable' : editor.checking ? 'Checking…' : editor.diagnostics.length ? 'Code needs attention' : editor.artifact ? 'All checks passed' : 'Waiting for Python…'}</span></div>
    <div id="diagnostics" className="diagnostics" role="status" aria-live="polite">{editor.diagnostics.length ? editor.diagnostics.map((item, index) => <button key={index} className="diagnostic" onClick={() => focusDiagnostic(item)}><strong>Line {item.line}:{item.column}</strong><span>{item.message}</span></button>) : <p className={editor.artifact ? 'check-ok' : 'muted'}>{editor.compiler === 'error' ? editor.message : editor.artifact ? '✓ Code checks passed. The compiled program is ready.' : editor.message}</p>}</div>
  </section>;
}

export function Develop({ active }: { active: boolean }) {
  const { compiler } = useRuntime();
  useEffect(() => { if (active) compiler.start(); }, [active, compiler]);
  return <><div className="intro"><div><p className="eyebrow">01 / EDITOR</p><h1>A little code. A lot of light.</h1><p>Create animations and transitions, one file at a time.</p></div>
    <ol className="workflow-steps" aria-label="Editor workflow"><li>Edit</li><li>Preview</li><li>Save locally</li><li>Deploy to device</li></ol></div>
    <DevelopmentWorkflow editorPanel={<Editor />} />
  </>;
}
