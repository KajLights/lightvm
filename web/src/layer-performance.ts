import type { CompositionState } from './types';

export function layerRates(before: CompositionState | null, after: CompositionState) {
  const elapsed = before?.sampled_at_us !== undefined && after.sampled_at_us !== undefined
    ? (after.sampled_at_us - before.sampled_at_us) / 1e6 : 0;
  return new Map((after.layers ?? []).map(layer => {
    const previous = before?.layers?.find(item => item.id === layer.id && layer.generation !== undefined && item.generation === layer.generation);
    const rate = (key: 'frames' | 'distinct_presentations') => {
      const a = previous?.[key], b = layer[key];
      return elapsed > 0 && a !== undefined && b !== undefined && b >= a ? (b - a) / elapsed : null;
    };
    return [layer.id, { updates: rate('frames'), displayed: rate('distinct_presentations') }];
  }));
}
