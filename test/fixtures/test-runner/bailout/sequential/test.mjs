import { describe, it } from 'node:test';

describe('sequential', { concurrency: false }, () => {
  it('first failing test', async () => {
    throw new Error('First test failed');
  });

  it('second test that should not run', () => {
    throw new Error('This test should not run');
  });

  it('third test that should not run', () => {
    throw new Error('This test should not run');
  });
});
