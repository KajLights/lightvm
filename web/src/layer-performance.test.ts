import { expect, it } from 'vitest';
import { layerRates } from './layer-performance';
import type { CompositionState } from './types';
const state = (time: number, frames: number, displayed: number, generation = 1): CompositionState => ({ active: true, sampled_at_us: time,
  layers: [{ id: 'bg', program: 'colour', generation, frames, distinct_presentations: displayed }] });
it('separates completed updates from displayed contributions, including cached images', () => {
  expect(layerRates(state(0, 10, 3), state(500000, 14, 4)).get('bg')).toEqual({ updates: 8, displayed: 2 });
  expect(layerRates(state(0, 10, 3), state(500000, 10, 3)).get('bg')).toEqual({ updates: 0, displayed: 0 });
});
it('does not turn replacement, restart, or missing samples into rates', () => {
  for (const before of [null, state(0, 5, 4, 2), state(500000, 5, 4), state(0, 20, 20)]) {
    expect(layerRates(before, state(500000, 10, 10)).get('bg')).toEqual({ updates: null, displayed: null });
  }
});
