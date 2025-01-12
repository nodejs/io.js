'use strict';

const common = require('../common');

const { test } = require('node:test');
const { Worker } = require('node:worker_threads');
const { once } = require('node:events');
const assert = require('node:assert');

// Test for ERR_WORKER_NOT_RUNNING when calling getHeapSnapshot on a worker that's not running
test('Worker heap snapshot tests', async (t) => {
  await t.test('should throw ERR_WORKER_NOT_RUNNING when worker is not running', async () => {
    const w = new Worker('', { eval: true });

    await once(w, 'exit');
    await assert.rejects(() => w.getHeapSnapshot(), {
      name: 'Error',
      code: 'ERR_WORKER_NOT_RUNNING',
    });
  });

  await t.test('should throw ERR_INVALID_ARG_TYPE for invalid options argument in getHeapSnapshot', async () => {
    const worker = new Worker('setInterval(() => {}, 1000);', { eval: true });
    await once(worker, 'online');

    // Test various invalid types for the `options` argument
    [1, true, [], null, Infinity, NaN].forEach((i) => {
      assert.throws(() => worker.getHeapSnapshot(i), {
        code: 'ERR_INVALID_ARG_TYPE',
        name: 'TypeError',
        message: `The "options" argument must be of type object.${common.invalidArgTypeHelper(i)}`,
      });
    });

    await worker.terminate();
  });
});
