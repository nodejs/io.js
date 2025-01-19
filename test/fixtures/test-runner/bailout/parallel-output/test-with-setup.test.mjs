import { existsSync } from 'node:fs';
import { describe, it } from 'node:test';

const testSyncPath = process.env.__TEST_SYNC_PATH__;
const testSyncFile = process.env.__TEST_SYNC_FILE__;

describe('test-with-setup', { concurrency: false }, () => {
  it('awaits second test to start running before passing', async (t) => {
    // Wait for the second test to run and create the file
    await t.waitFor(
      () => {
        const exist = existsSync(`${testSyncPath}/${testSyncFile}`);
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

  it('first', async () => {
    throw new Error('First test failed');
  });

  it('second', () => {
    throw new Error('Second test should not run');
  });
});
