'use strict';

const common = require('../common');
const { once } = require('node:events');
const {
  parentPort,
  postMessageToThread,
  threadId,
  Worker,
  workerData,
} = require('node:worker_threads');
const { rejects } = require('node:assert');

async function test() {
  await rejects(common.mustCall(function() {
    return postMessageToThread(threadId);
  }), {
    name: 'Error',
    code: 'ERR_WORKER_MESSAGING_SAME_THREAD',
  });

  await rejects(common.mustCall(function() {
    return postMessageToThread(Date.now());
  }), {
    name: 'Error',
    code: 'ERR_WORKER_MESSAGING_FAILED',
  });

  // The delivery to the first worker will fail as there is no listener for `workerMessage`
  const worker = new Worker(__filename, { workerData: { children: true } });
  await once(worker, 'message');

  await rejects(common.mustCall(function() {
    return postMessageToThread(worker.threadId);
  }), {
    name: 'Error',
    code: 'ERR_WORKER_MESSAGING_FAILED',
  });

  worker.postMessage('success');
}

if (!workerData?.children) {
  test();
} else {
  parentPort.postMessage('ready');
  parentPort.once('message', common.mustCall());
}
