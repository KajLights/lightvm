import catalogue from '../../examples/lessons/catalogue.json';
import type { Language } from './languages/source';
const files = import.meta.glob('../../examples/lessons/*/*.{py,js,json}', { query: '?raw', import: 'default' }) as Record<string, () => Promise<string>>;
export const LESSONS = catalogue;
export type LessonVersion = 'starter' | 'solution' | 'extension' | 'bug';
export async function lessonSource(id: string, version: LessonVersion, language: Language) {
  const extension = { python: 'py', javascript: 'js', blocks: 'blocks.json' }[language];
  return { source: await files[`../../examples/lessons/${id}/${version}.${extension}`](), filename: `${id}-${version}.${extension}` };
}
