'use strict';
require('../common');
const Countdown = require('../common/countdown');
const assert = require('assert');
const { Worker } = require('worker_threads');
const { describe, it, mock } = require('node:test');

describe('Allocating uninitialized ArrayBuffers ...', () => {
  it('...should not affect zero-fill in other threads', () => {
    const w = new Worker(`
      const { parentPort } = require('worker_threads');

      function post() {
        const uint32array = new Uint32Array(64);
        parentPort.postMessage(uint32array.reduce((a, b) => a + b));
      }

      setInterval(post, 0);
    `, { eval: true });

    const fn = mock.fn(() => {
      // Continuously allocate memory in the main thread. The allocUnsafe
      // here sets a scope internally that indicates that the memory should
      // not be initialized. While this is happening, the other thread is
      // also allocating buffers that must remain zero-filled. The purpose
      // of this test is to ensure that the scope used to determine whether
      // to zero-fill or not does not impact the other thread.
      setInterval(() => Buffer.allocUnsafe(32 * 1024 * 1024), 0).unref();
    });

    w.on('online', fn);

    const countdown = new Countdown(100, () => {
      w.terminate();
      assert(fn.mock.calls.length > 0);
    });

    w.on('message', (sum) => {
      assert.strictEqual(sum, 0);
      if (countdown.remaining) countdown.dec();
    });
  });
});
