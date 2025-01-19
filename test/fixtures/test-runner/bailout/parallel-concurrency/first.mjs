import { existsSync } from 'node:fs';
import { describe, it } from 'node:test';

const testSyncPath = process.env.__TEST_SYNC_PATH__;

describe('first parallel file', () => {
  it('awaits second test to start running before passing', async (t) => {
    // Wait for the second test to run and create the file
    await t.waitFor(
      () => {
        const exist = existsSync(`${testSyncPath}/second-file`);
        if (!exist) {
          throw new Error('Second file does not exist yet');
        }
      },
      {
        interval: 1000,
        timeout: 60_000,
      },
    );
  });

  it('failing test', () => {
    throw new Error('First test failed');
  });
});
