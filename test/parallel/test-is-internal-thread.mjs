import { spawnPromisified } from '../common/index.mjs';
import * as fixtures from '../common/fixtures.mjs';
import assert from 'node:assert';
import { execPath } from 'node:process';
import { describe, it } from 'node:test';
import { isInternalThread, Worker } from 'node:worker_threads';
import * as common from '../common/index.mjs';

describe('worker_threads.isInternalThread', { concurrency: !process.env.TEST_PARALLEL }, () => {
  it('should be true inside the loader thread', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      '--no-warnings',
      '--experimental-loader',
      fixtures.fileURL('loader-is-internal-thread.js'),
      '--eval',
      'setTimeout(() => {},99)',
    ]);

    assert.strictEqual(stderr, '');
    assert.match(stdout, /isInternalThread: true/);
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);
  });

  it('should be false inside the main thread', async () => {
    assert.strictEqual(isInternalThread, false);
  });

  it('should be false inside a regular worker thread', async () => {
    const worker = new Worker(fixtures.path('worker-is-internal-thread.js'));

    worker.on('message', common.mustCall((message) => {
      assert.strictEqual(message, 'isInternalThread: false');
    }));
  });
});
