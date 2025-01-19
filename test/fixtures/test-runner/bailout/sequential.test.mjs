import { describe, it } from 'node:test';

describe('sequential', { concurrency: false }, () => {
  it('first', async (t) => {
    await new Promise((resolve) => setTimeout(resolve, 500));
    t.assert.equal(1, 1);
  });

  it('second', async () => {
    throw new Error('First test failed');
  });

  it('third', async () => {
    throw new Error('Second test should not run');
  });
});
