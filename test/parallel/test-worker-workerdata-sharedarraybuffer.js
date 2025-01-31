// Flags: --expose-gc
'use strict';

const common = require('../common');
const assert = require('assert');
const { Worker } = require('worker_threads');

{
  const sharedArrayBuffer = new SharedArrayBuffer(12);
  const local = Buffer.from(sharedArrayBuffer);

  const w = new Worker(`
    const { parentPort, workerData } = require('worker_threads');
    const local = Buffer.from(workerData.sharedArrayBuffer);

    parentPort.on('message', () => {
      local.write('world!', 6);
      parentPort.postMessage('written!');
    });
  `, {
    eval: true,
    workerData: { sharedArrayBuffer }
  });
  w.on('message', common.mustCall(() => {
    assert.strictEqual(local.toString(), 'Hello world!');
    globalThis.gc();
    w.terminate();
  }));
  w.postMessage({});
  // This would be a race condition if the memory regions were overlapping
  local.write('Hello ');
}
