'use strict';

const common = require('../common');
const {
  postMessageToThread,
  workerData,
  Worker,
} = require('node:worker_threads');
const { rejects } = require('node:assert');

const memory = new SharedArrayBuffer(4);

async function test() {
  const worker = new Worker(__filename, { workerData: { memory, children: true } });
  const array = new Int32Array(memory);

  await rejects(common.mustCall(function() {
    return postMessageToThread(worker.threadId, 0, common.platformTimeout(500));
  }), {
    name: 'Error',
    code: 'ERR_WORKER_MESSAGING_TIMEOUT',
  });

  Atomics.store(array, 0, 1);
  Atomics.notify(array, 0);
}

if (!workerData?.children) {
  test();
} else {
  process.on('beforeExit', common.mustCall());

  const array = new Int32Array(workerData.memory);

  // Starve this thread waiting for the status to be unlocked.
  // This happens in the main thread AFTER the timeout.
  Atomics.wait(array, 0, 0);
}
