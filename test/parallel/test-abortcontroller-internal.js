// Flags: --no-warnings --expose-gc --expose-internals
'use strict';
require('../common');

const {
  strictEqual,
} = require('assert');

const {
  test,
} = require('node:test');

const {
  kWeakHandler,
} = require('internal/event_target');

const { setTimeout: sleep } = require('timers/promises');

// The tests in this file depend on Node.js internal APIs. These are not necessarily
// portable to other runtimes

test('A weak event listener should not prevent gc', async () => {
  // If the event listener is weak, however, it should not prevent gc
  let ref;
  function handler() {}
  {
    ref = new globalThis.WeakRef(AbortSignal.timeout(1_200_000));
    ref.deref().addEventListener('abort', handler, { [kWeakHandler]: {} });
  }

  await sleep(10);
  globalThis.gc();
  strictEqual(ref.deref(), undefined);
});
