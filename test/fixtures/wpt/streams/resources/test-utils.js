'use strict';

self.delay = ms => new Promise(resolve => step_timeout(resolve, ms));

// For tests which verify that the implementation doesn't do something it shouldn't, it's better not to use a
// timeout. Instead, assume that any reasonable implementation is going to finish work after 2 times around the event
// loop, and use flushAsyncEvents().then(() => assert_array_equals(...));
// Some tests include promise resolutions which may mean the test code takes a couple of event loop visits itself. So go
// around an extra 2 times to avoid complicating those tests.
self.flushAsyncEvents = () => delay(0).then(() => delay(0)).then(() => delay(0)).then(() => delay(0));

self.assert_typed_array_equals = (actual, expected, message) => {
  const prefix = message === undefined ? '' : `${message} `;
  assert_equals(typeof actual, 'object', `${prefix}type is object`);
  assert_equals(actual.constructor, expected.constructor, `${prefix}constructor`);
  assert_equals(actual.byteOffset, expected.byteOffset, `${prefix}byteOffset`);
  assert_equals(actual.byteLength, expected.byteLength, `${prefix}byteLength`);
  assert_equals(actual.buffer.byteLength, expected.buffer.byteLength, `${prefix}buffer.byteLength`);
  assert_array_equals([...actual], [...expected], `${prefix}contents`);
  assert_array_equals([...new Uint8Array(actual.buffer)], [...new Uint8Array(expected.buffer)], `${prefix}buffer contents`);
};

self.makePromiseAndResolveFunc = () => {
  let resolve;
  const promise = new Promise(r => { resolve = r; });
  return [promise, resolve];
};
