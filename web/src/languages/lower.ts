import { lowerJavaScript } from './javascript';
import { lowerBlocks } from './blocks';
import { SourceError, type Language, type LoweredSource } from './source';
export function lowerSource(source: string, language: Language): LoweredSource {
  if (new TextEncoder().encode(source).length > 65536) throw new SourceError('Keep source below 64 KiB.');
  if (language === 'javascript') return lowerJavaScript(source);
  if (language === 'blocks') return lowerBlocks(source);
  return { python: source, locations: source.split('\n').map((_, i) => ({ line: i + 1, column: 1 })) };
}
