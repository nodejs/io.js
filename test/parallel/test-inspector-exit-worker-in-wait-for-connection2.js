'use strict';

const common = require('../common');
common.skipIfInspectorDisabled();

const { isMainThread, workerData, Worker } = require('node:worker_threads');
if (!workerData && !isMainThread) {
  common.skip('This test only works on a main thread');
}

const assert = require('node:assert');

let TIMEOUT = common.platformTimeout(5000);
if (common.isWindows) {
  // Refs: https://github.com/nodejs/build/issues/3014
  TIMEOUT = common.platformTimeout(15000);
}

// Refs: https://github.com/nodejs/node/issues/53648

(async () => {
  if (!workerData) {
    // worker.terminate() should terminate the worker created with execArgv:
    // ["--inspect-brk"].
    {
      const worker = new Worker(__filename, {
        execArgv: ['--inspect-brk=0'],
        workerData: {},
      });
      await new Promise((r) => setTimeout(r, TIMEOUT));
      worker.on('exit', common.mustCall());
      await worker.terminate();
    }
    // process.exit() should kill the process.
    {
      new Worker(__filename, {
        execArgv: ['--inspect-brk=0'],
        workerData: {},
      });
      await new Promise((r) => setTimeout(r, TIMEOUT));
      process.on('exit', (status) => assert.strictEqual(status, 0));
      setImmediate(() => process.exit());
    }
  } else {
    console.log('Worker running!');
  }
})().then(common.mustCall());
