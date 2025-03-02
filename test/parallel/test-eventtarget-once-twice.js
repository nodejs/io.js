'use strict';
const common = require('../common');
const { once } = require('node:events');
const { test } = require('node:test');

test('EventTarget test', async () => {
  const et = new EventTarget();

  // Use `once` to listen for the 'foo' event twice
  const promise1 = once(et, 'foo');
  const promise2 = once(et, 'foo');

  // Dispatch the first event
  et.dispatchEvent(new Event('foo'));

  // Dispatch the second event in the next tick to ensure it's awaited properly
  setImmediate(() => {
    et.dispatchEvent(new Event('foo'));
  });

  // Await both promises to ensure both 'foo' events are captured
  await promise1;
  await promise2;

  // Test automatically completes after all asynchronous operations finish
}).then(common.mustCall());
