'use strict';

const common = require('../common');
const {
  parentPort,
  postMessageToThread,
  Worker,
  workerData,
} = require('node:worker_threads');
const { rejects } = require('node:assert');

async function test() {
  const worker = new Worker(__filename, { workerData: { children: true } });

  await rejects(common.mustCall(function() {
    return postMessageToThread(worker.threadId);
  }), {
    name: 'Error',
    code: 'ERR_WORKER_MESSAGING_ERRORED',
  });

  worker.postMessage('success');
}

if (!workerData?.children) {
  test();
} else {
  process.on('workerMessage', () => {
    throw new Error('KABOOM');
  });

  parentPort.postMessage('ready');
  parentPort.once('message', common.mustCall());
}
