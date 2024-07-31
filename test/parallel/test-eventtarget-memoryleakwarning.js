// Flags: --expose-internals --no-warnings --expose-gc
'use strict';
const common = require('../common');
const {
  setMaxListeners,
  EventEmitter,
} = require('events');
const assert = require('assert');
const { kWeakHandler } = require('internal/event_target');
const { setTimeout } = require('timers/promises');

common.expectWarning({
  MaxListenersExceededWarning: [
    ['Possible EventTarget memory leak detected. 3 foo listeners added to ' +
     'EventTarget. MaxListeners is 2. Use events.setMaxListeners() ' +
     'to increase limit'],
    ['Possible EventTarget memory leak detected. 3 foo listeners added to ' +
     '[MessagePort [EventTarget]]. ' +
     'MaxListeners is 2. ' +
     'Use events.setMaxListeners() to increase ' +
     'limit'],
    ['Possible EventTarget memory leak detected. 3 foo listeners added to ' +
     '[MessagePort [EventTarget]]. ' +
     'MaxListeners is 2. ' +
     'Use events.setMaxListeners() to increase ' +
     'limit'],
    ['Possible EventTarget memory leak detected. 3 foo listeners added to ' +
     '[AbortSignal]. ' +
     'MaxListeners is 2. ' +
     'Use events.setMaxListeners() to increase ' +
     'limit'],
  ],
});


{
  const et = new EventTarget();
  setMaxListeners(2, et);
  et.addEventListener('foo', () => {});
  et.addEventListener('foo', () => {});
  et.addEventListener('foo', () => {});
}

{
  // No warning emitted because prior limit was only for that
  // one EventTarget.
  const et = new EventTarget();
  et.addEventListener('foo', () => {});
  et.addEventListener('foo', () => {});
  et.addEventListener('foo', () => {});
}

{
  const mc = new MessageChannel();
  setMaxListeners(2, mc.port1);
  mc.port1.addEventListener('foo', () => {});
  mc.port1.addEventListener('foo', () => {});
  mc.port1.addEventListener('foo', () => {});
}

{
  // Set the default for newly created EventTargets
  setMaxListeners(2);
  const mc = new MessageChannel();
  mc.port1.addEventListener('foo', () => {});
  mc.port1.addEventListener('foo', () => {});
  mc.port1.addEventListener('foo', () => {});

  const ac = new AbortController();
  ac.signal.addEventListener('foo', () => {});
  ac.signal.addEventListener('foo', () => {});
  ac.signal.addEventListener('foo', () => {});
}

{
  // It works for EventEmitters also
  const ee = new EventEmitter();
  setMaxListeners(2, ee);
  assert.strictEqual(ee.getMaxListeners(), 2);
}

{
  (async () => {
    // Test that EventTarget listener don't emit MaxListenersExceededWarning for weak listeners that GCed
    const et = new EventTarget();
    setMaxListeners(2, et);

    for (let i = 0; i <= 3; i++) {
      et.addEventListener('foo', () => {}, {
        [kWeakHandler]: {},
      });

      await setTimeout(0);
      global.gc();
    }
  })().then(common.mustCall(), common.mustNotCall());
}
