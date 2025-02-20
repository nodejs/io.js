'use strict';

const { mustCall, platformTimeout, hasCrypto, skip, isSunOS } = require('../common');

if (!hasCrypto) {
  skip('missing crypto');
};

// This block can be removed once SmartOS support is fixed in
// https://github.com/libuv/libuv/issues/4706
// The behavior on SunOS is tested in
// test/parallel/test-process-threadCpuUsage-main-thread.js
if (isSunOS) {
  skip('Operation not supported yet on SmartOS');
}

const { ok, deepStrictEqual } = require('assert');
const { randomBytes, createHash } = require('crypto');
const { once } = require('events');
const { Worker, isMainThread, parentPort, threadId, workerData } = require('worker_threads');

const FREQUENCIES = [100, 500, 1000];

function performLoad() {
  const buffer = randomBytes(1e8);

  // Do some work
  return setInterval(() => {
    createHash('sha256').update(buffer).end(buffer);
  }, platformTimeout(workerData?.frequency ?? 100));
}

function getUsages() {
  return { threadId, process: process.cpuUsage(), thread: process.threadCpuUsage() };
}

function validateResults(results) {
  for (let i = 0; i < 4; i++) {
    deepStrictEqual(results[i].threadId, i);
  }

  // This test should have checked that the CPU usage of each thread is greater
  // than the previous one, while the process one was not.
  // Unfortunately, the real values are not really predictable on the CI so we
  // just check that all the values are positive numbers.
  for (let i = 0; i < 3; i++) {
    ok(typeof results[i].process.user === 'number');
    ok(results[i].process.user >= 0);

    ok(typeof results[i].process.system === 'number');
    ok(results[i].process.system >= 0);

    ok(typeof results[i].thread.user === 'number');
    ok(results[i].thread.user >= 0);

    ok(typeof results[i].thread.system === 'number');
    ok(results[i].thread.system >= 0);
  }
}

// The main thread will spawn three more threads, then after a while it will ask all of them to
// report the thread CPU usage and exit.
if (isMainThread) {
  const workers = [];
  for (const frequency of FREQUENCIES) {
    workers.push(new Worker(__filename, { workerData: { frequency } }));
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
