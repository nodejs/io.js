'use strict';

require('../common');
const { ok, strictEqual } = require('assert');
const { setImmediate: sleep } = require('timers/promises');
const {
  transferableAbortSignal,
  transferableAbortController,
} = require('util');
const {
  test,
  mock,
} = require('node:test');

test('Can create a transferable abort controller', async () => {
  const ac = transferableAbortController();
  const mc = new MessageChannel();

  const setup1 = Promise.withResolvers();
  const setup2 = Promise.withResolvers();
  const setupResolvers = [setup1, setup2];

  const abort1 = Promise.withResolvers();
  const abort2 = Promise.withResolvers();
  const abort3 = Promise.withResolvers();
  const abortResolvers = [abort1, abort2, abort3];

  mc.port1.onmessage = ({ data }) => {
    data.addEventListener('abort', () => {
      strictEqual(data.reason, 'boom');
      abortResolvers.shift().resolve();
    });
    setupResolvers.shift().resolve();
  };

  mc.port2.postMessage(ac.signal, [ac.signal]);

  // Can be cloned/transferd multiple times and they all still work
  mc.port2.postMessage(ac.signal, [ac.signal]);

  // Although we're using transfer semantics, the local AbortSignal
  // is still usable locally.
  ac.signal.addEventListener('abort', () => {
    strictEqual(ac.signal.reason, 'boom');
    abortResolvers.shift().resolve();
  });

  await Promise.all([ setup1.promise, setup2.promise ]);

  ac.abort('boom');

  await Promise.all([ abort1.promise, abort2.promise, abort3.promise ]);

  mc.port2.close();

});

test('Can create a transferable abort signal', async () => {
  const signal = transferableAbortSignal(AbortSignal.abort('boom'));
  ok(signal.aborted);
  strictEqual(signal.reason, 'boom');
  const mc = new MessageChannel();
  const { promise, resolve } = Promise.withResolvers();
  mc.port1.onmessage = ({ data }) => {
    ok(data instanceof AbortSignal);
    ok(data.aborted);
    strictEqual(data.reason, 'boom');
    resolve();
  };
  mc.port2.postMessage(signal, [signal]);
  await promise;
  mc.port1.close();
});

test('A cloned AbortSignal does not keep the event loop open', async () => {
  const ac = transferableAbortController();
  const mc = new MessageChannel();
  const fn = mock.fn();
  mc.port1.onmessage = fn;
  mc.port2.postMessage(ac.signal, [ac.signal]);
  // Because the postMessage used by the underlying AbortSignal
  // takes at least one turn of the event loop to be processed,
  // and because it is unref'd, it won't, by itself, keep the
  // event loop open long enough for the test to complete, so
  // we schedule two back to back turns of the event to ensure
  // the loop runs long enough for the test to complete.
  await sleep();
  await sleep();
  strictEqual(fn.mock.calls.length, 1);
  mc.port2.close();
});
