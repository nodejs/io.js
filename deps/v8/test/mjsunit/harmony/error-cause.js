// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-error-cause

// Basic error
(function () {
  const err = Error('message', { cause: 'a cause' });
  assertEquals('a cause', err.cause);
  const descriptor = Object.getOwnPropertyDescriptor(err, 'cause');
  assertEquals('a cause', descriptor.value);
  assertFalse(descriptor.enumerable);
  assertTrue(descriptor.writable);
  assertTrue(descriptor.configurable);
})();

// No cause
(function () {
  const err = Error('message');
  assertEquals(undefined, err.cause);
  assertFalse('cause' in err);
  assertFalse('cause' in Error.prototype);
})();

// Chained errors
(function () {
  async function fail() { throw new Error('caused by fail') }

  async function doJob() {
    await fail()
      .catch(err => {
        throw new Error('Found an error', { cause: err });
      });
  }

  async function main() {
    try {
      await doJob();
    } catch (e) {
      assertEquals('Found an error', e.message);
      assertEquals('caused by fail', e.cause.message);
    }
  }

  main();
})();

// AggregateError with cause
(function() {
  const err = AggregateError([1, 2, 3], 'aggregate errors', { cause: 'a cause' });
  assertEquals('a cause', err.cause);
  const descriptor = Object.getOwnPropertyDescriptor(err, 'cause');
  assertEquals('a cause', descriptor.value);
  assertFalse(descriptor.enumerable);
  assertTrue(descriptor.writable);
  assertTrue(descriptor.configurable);
})();

// Options is not an object
(function () {
  const err1 = Error('message', 42);
  assertEquals(undefined, err1.cause);
  const err2 = Error('message', null);
  assertEquals(undefined, err2.cause);
  const err3 = Error('message', [42]);
  assertEquals(undefined, err3.cause);
})();

// Options object does not contain 'cause'
(function () {
  const err = Error('message', { syy: 'A finnish cause' });
  assertEquals(undefined, err.cause);
  assertEquals(undefined, err.syy);
})();

// Options is a proxy
(function () {
  const options = { cause: 'a cause' };
  const proxy = new Proxy(options, { get: () => 'proxied cause'});
  const err = Error('message', proxy);
  assertEquals('proxied cause', err.cause);
})();

// Options is a proxy, but does not expose 'cause'
(function () {
  const options = { cause: 'a cause' };
  const proxy = new Proxy(options, {
    has: (_, key) => key != 'cause',
    get: () => 'proxied cause'
  })
  const err = Error('message', proxy);
  assertEquals(undefined, err.cause);
})();

// Options is a proxy, throw in 'get'
(function () {
  const options = { cause: 'a cause' };
  const proxy = new Proxy(options, {
    get: () => {  throw Error('proxy get', { cause: 'no reason' }) },
  });
  try {
    Error('message', proxy);
    assertUnreachable();
  } catch(e) {
    assertEquals('proxy get', e.message);
    assertEquals('no reason', e.cause);
  }
})();

// Options is a proxy, throw in 'has'
(function () {
  const options = { cause: 'a cause' };
  const proxy = new Proxy(options, {
    has: () => { throw Error('proxy has', { cause: 'no reason' }) },
  });
  try {
    Error('message', proxy);
    assertUnreachable();
  } catch(e) {
    assertEquals('proxy has', e.message);
    assertEquals('no reason', e.cause);
  }
})();

// Cause in the options prototype chain
(function () {
  function Options() {};
  Options.prototype.cause = 'cause in the prototype';
  const options = new Options();
  const err = Error('message', options);
  assertEquals('cause in the prototype', err.cause);
})();

// Cause in the options prototype chain proxy
(function () {
  function Options() {};
  Options.prototype = new Proxy({ cause: 42 }, {
    get: (_ ,name) => {
      assertEquals('cause', name);
      return 'cause in the prototype'
  }});
  const options = new Options();
  const err = Error('message', options);
  assertEquals('cause in the prototype', err.cause);
})();

// Cause in the options prototype chain proxy and throw
(function () {
  function Options() {};
  Options.prototype = new Proxy({ cause: 42 }, { get: () => { throw Error('cause in the prototype')}  });
  const options = new Options();
  try {
    Error('message', options);
    assertUnreachable();
  } catch(e) {
    assertEquals('cause in the prototype', e.message);
  }
})();

// Change Error.cause in the prototype
(function () {
  Error.prototype.cause = 'main cause';
  const err1 = new Error('err1');
  assertEquals('main cause', err1.cause);
  const desc1 = Object.getOwnPropertyDescriptor(err1, 'cause');
  assertEquals(undefined, desc1);

  const err2 = new Error('err2', { cause: 'another cause' });
  assertEquals(err2.cause, 'another cause');
  const desc2 = Object.getOwnPropertyDescriptor(err2, 'cause');
  assertFalse(desc2.enumerable);
  assertTrue(desc2.writable);
  assertTrue(desc2.configurable);

  const err3 = new Error('err3');
  err3.cause = 'after err3';
  assertEquals('after err3', err3.cause);
  const desc3 = Object.getOwnPropertyDescriptor(err3, 'cause');
  assertTrue(desc3.enumerable);
  assertTrue(desc3.writable);
  assertTrue(desc3.configurable);
})();

// Change prototype to throw in setter
(function() {
  const my_proto = {};
  Object.defineProperty(my_proto, 'cause', {set: function(x) { throw 'foo' } });
  Error.prototype.__proto__ = my_proto
  const err = Error('message', { cause: 'a cause' });
  assertEquals('a cause', err.cause);
})();

// Use defineProperty to change Error cause and throw
(function () {
  Object.defineProperty(Error.prototype, 'cause', {
    get: () => { throw Error('from get', { cause: 'inside get' }) },
  });
  const err1 = new Error('err1');
  try {
    err1.cause;
    assertUnreachable();
  } catch(e) {
    assertEquals('from get', e.message);
    assertEquals('inside get', e.cause);
  }
  const err2 = new Error('err2', { cause: 'another cause' });
  assertEquals('another cause', err2.cause);
})();

// Serialize and deserialize error object
(function () {
  const err = Error('message', { cause: 'a cause' });
  const worker = new Worker('onmessage = ({data:msg}) => postMessage(msg)', { type: 'string' });
  worker.postMessage(err);
  const serialized_err = worker.getMessage();
  assertEquals(err, serialized_err);
  const descriptor = Object.getOwnPropertyDescriptor(serialized_err, 'cause');
  assertEquals(descriptor.value, 'a cause');
  assertFalse(descriptor.enumerable);
  assertTrue(descriptor.writable);
  assertTrue(descriptor.configurable);
})();
