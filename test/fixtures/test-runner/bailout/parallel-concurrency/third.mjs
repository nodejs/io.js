import { describe, it } from 'node:test';

describe('third parallel file', () => {
  it('test that should never start', () => {
    throw new Error('This test should never start');
  });
});
