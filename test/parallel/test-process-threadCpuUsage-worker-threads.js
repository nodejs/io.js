'use strict';

const { mustCall, platformTimeout, hasCrypto, skip } = require('../common');
const { ok, deepStrictEqual } = require('assert');
const { randomBytes, createHash } = require('crypto');
const { once } = require('events');
const { Worker, isMainThread, parentPort, threadId } = require('worker_threads');

if (!hasCrypto) {
  skip('missing crypto');
};

function performLoad() {
  const buffer = randomBytes(1e8);
  const index = threadId + 1;

  // Do some work
  return setInterval(() => {
    createHash('sha256').update(buffer).end(buffer);
  }, platformTimeout(index ** 2 * 100));
}

function getUsages() {
  return { threadId, process: process.cpuUsage(), thread: process.threadCpuUsage() };
}

function validateResults(results) {
  for (let i = 0; i < 4; i++) {
    deepStrictEqual(results[i].threadId, i);
  }

  for (let i = 0; i < 3; i++) {
    const processDifference = results[i].process.user / results[i + 1].process.user;
    const threadDifference = results[i].thread.user / results[i + 1].thread.user;

    //
    // All process CPU usages should be the same. Technically they should have returned the same
    // value but since we measure it at different times they vary a little bit.
    // Let's allow a tolerance of 20%
    //
    ok(processDifference > 0.8);
    ok(processDifference < 1.2);

    //
    // Each thread is configured so that the performLoad schedules a new hash with an interval two times bigger of the
    // previous thread. In theory this should give each thread a load about half of the previous one.
    // But since we can't really predict CPU scheduling, we just check a monotonic increasing sequence.
    //
    ok(threadDifference > 1.2);
  }
}


// The main thread will spawn three more threads, then after a while it will ask all of them to
// report the thread CPU usage and exit.
if (isMainThread) {
  const workers = [];
  for (let i = 0; i < 3; i++) {
    workers.push(new Worker(__filename));
  }

  setTimeout(mustCall(async () => {
    clearInterval(interval);

    const results = [getUsages()];

    for (const worker of workers) {
      const statusPromise = once(worker, 'message');
      const exitPromise = once(worker, 'exit');

      worker.postMessage('done');
      const [status] = await statusPromise;
      results.push(status);
      await exitPromise;
    }

    validateResults(results);
  }), platformTimeout(5000));

} else {
  parentPort.on('message', () => {
    clearInterval(interval);
    parentPort.postMessage(getUsages());
    process.exit(0);
  });
}

// Perform load on each thread
const interval = performLoad();
