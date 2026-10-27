import type { Diagnostic } from '../types';
export type Language = 'python' | 'javascript' | 'blocks';
export const LANGUAGE_LABELS: Record<Language, string> = { python: 'Python', javascript: 'JavaScript', blocks: 'Blocks' };
export const EXTENSIONS: Record<Language, string> = { python: '.py', javascript: '.js', blocks: '.blocks.json' };
export function languageForFilename(filename: string): Language {
  return /\.blocks\.json$/i.test(filename) ? 'blocks' : /\.js$/i.test(filename) ? 'javascript' : 'python';
}
export interface SourceLocation { line: number; column: number; blockId?: string }
export interface LoweredSource { python: string; locations: SourceLocation[]; names?: Record<string, string> }
export class SourceError extends Error {
  constructor(message: string, public location: SourceLocation = { line: 1, column: 1 }) { super(message); }
}
export function sourceDiagnostic(error: unknown): Diagnostic {
  return { message: error instanceof Error ? error.message : String(error), ...(error instanceof SourceError ? error.location : { line: 1, column: 1 }) };
}
export const SDK_IMPORTS = `from lightvm import profile, setting, Input, Ramp, rgb, hsv, hash01, perlin2, perlin3, simplex2, simplex3, init, render, PixelArray, Sprite, SpriteBatch, Transform, ColorTransform, red, green, blue, colorwheel, pico_colorwheel, hue_degrees, floor, ceil, trunc, fixed, fixed_bits, fixed_from_bits, fixed_sqrt, FixedArray
from lightvm.motion import repeat, pingpong, clamp01, lerp, fit_start, fit_end, wrap_index, wrap
from lightvm.easing import linear, smoothstep, quadratic_in, quadratic_out, quadratic_in_out, cubic_in, cubic_out, cubic_in_out, sine_in, sine_out, sine_in_out, circular_in, circular_out, circular_in_out, exponential_in, exponential_out, exponential_in_out
from lightvm.transitions import crossfade, wipe
from array import array
import math
import random`;
