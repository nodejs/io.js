import { writeFileSync } from 'node:fs';
import { describe, it } from 'node:test';
import { setTimeout } from 'node:timers/promises';

const testSyncPath = process.env.__TEST_SYNC_PATH__;

describe('second parallel file', () => {
  // Create a file to signal that the second test has started
  writeFileSync(`${testSyncPath}/second-file`, 'second file content');
  it('slow test that should be cancelled', async (t) => {
    while (true) {
      await setTimeout(5000);
    }
  });
});
