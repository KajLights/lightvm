import { describe, expect, it } from 'vitest';
import cases from '../../tests/release_cases.json';
import { BASELINE, programReleaseError, releaseError, VERSION } from './release';

describe('application release ordering', () => {
  it('matches the shared firmware/compiler compatibility cases', () => {
    for (const [current, required, accepted] of cases) expect(releaseError(current, required) === null, `${current} / ${required}`).toBe(accepted);
    expect(programReleaseError(VERSION, BASELINE)).toBeNull();
  });
  it('uses the current milestone baseline for program admission', () => {
    expect(VERSION).toBe('0.1.0');
    expect(BASELINE).toBe('0.1.0');
    for (const required of ['0.1', '0.1.0', '0.1.0+build.abc']) expect(programReleaseError(VERSION, required)).toBeNull();
    for (const required of ['0', '0.0.9', '0.1.0-alpha', '0.1.1', '0.2.0', '1.0.0', '1.4.0']) expect(programReleaseError(VERSION, required)).not.toBeNull();
  });
});
