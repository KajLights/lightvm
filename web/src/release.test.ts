import { describe, expect, it } from 'vitest';
import cases from '../../tests/release_cases.json';
import { releaseError, VERSION } from './release';

describe('application release ordering', () => {
  it('matches the shared firmware/compiler compatibility cases', () => {
    for (const [current, required, accepted] of cases) expect(releaseError(current, required) === null, `${current} / ${required}`).toBe(accepted);
    expect(releaseError(VERSION, '1')).toBeNull();
  });
});
