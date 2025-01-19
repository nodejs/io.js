import { writeFile } from 'node:fs/promises';
import { describe, it } from 'node:test';
import { setTimeout } from 'node:timers/promises';

const testSyncPath = process.env.__TEST_SYNC_PATH__;
const testSyncFile = process.env.__TEST_SYNC_FILE__;

describe('long-running-test', async () => {
  it('first', async (t) => {
    // Create a file to signal that the second test has started
    writeFile(`${testSyncPath}/${testSyncFile}`, 'file content');

    while (true) {
      // a long running test
      await setTimeout(5000);
    }
    throw new Error('Second test should not run');
  });
});
