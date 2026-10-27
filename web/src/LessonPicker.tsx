import { useEffect, useRef, useState } from 'react';
import { LESSONS, lessonSource, type LessonVersion } from './lessons';
import { useRuntime } from './runtime';
import { useAppSelector } from './store';

export function LessonPicker() {
  const language = useAppSelector(s => s.editor.language), { compiler } = useRuntime();
  const [id, setId] = useState('');
  const [error, setError] = useState(''), pending = useRef(0);
  useEffect(() => { pending.current++; }, [language]);
  const lesson = LESSONS.find(lesson => lesson.id === id);
  async function load(version: LessonVersion, selected = id) {
    const request = ++pending.current; setError('');
    try {
      const source = await lessonSource(selected, version, language);
      if (pending.current === request) compiler.edit(source.source, source.filename);
    } catch { if (pending.current === request) setError('This lesson file could not load. Check that the complete editor folder is being served.'); }
  }
  return <div className="lesson-picker">
    {error && <p role="alert">{error}</p>}
    <label>Learn with lights <select aria-label="Choose a lesson" value={id} onChange={event => { setId(event.target.value); if (event.target.value) load('starter', event.target.value); }}>
      <option value="">Choose a lesson…</option>{LESSONS.map((lesson, i) => <option value={lesson.id} key={lesson.id}>{i + 1}. {lesson.title}</option>)}
    </select></label>
    {lesson && <>
      <p><strong>{lesson.problem}</strong></p><p>{lesson.idea}</p>
      <ol><li>Predict: {lesson.predict}</li><li>Try: {lesson.task}</li><li>Check: {lesson.check}</li></ol>
      <p>Build on it: {lesson.extension}</p>
      <div className="preview-controls"><button className="quiet" onClick={() => load('starter')}>Open starter</button><button className="quiet" onClick={() => load('solution')}>See one solution</button><button className="quiet" onClick={() => load('extension')}>Try the next change</button><button className="quiet" onClick={() => load('bug')}>Find a mistake</button></div>
      <details><summary>Teaching notes</summary><p>{lesson.explain}</p><p>{lesson.bug}</p></details>
      <p className="helper">Save your file before opening another version. Each button opens the version for your selected language.</p>
    </>}
  </div>;
}
