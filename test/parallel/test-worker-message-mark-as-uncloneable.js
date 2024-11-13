'use strict';

require('../common');
const assert = require('assert');
const { markAsUncloneable } = require('node:worker_threads');
const { mustCall } = require('../common');

const expectedErrorName = 'DataCloneError';

// Uncloneables cannot be cloned during message posting
{
  const anyObject = { foo: 'bar' };
  markAsUncloneable(anyObject);
  const { port1 } = new MessageChannel();
  assert.throws(() => port1.postMessage(anyObject), {
    constructor: DOMException,
    name: expectedErrorName,
    code: 25,
  }, `Should throw ${expectedErrorName} when posting uncloneables`);
}

// Uncloneables cannot be cloned during structured cloning
{
  class MockResponse extends Response {
    constructor() {
      super();
      markAsUncloneable(this);
    }
  }
  structuredClone(MockResponse.prototype);

  markAsUncloneable(MockResponse.prototype);
  const r = new MockResponse();
  assert.throws(() => structuredClone(r), {
    constructor: DOMException,
    name: expectedErrorName,
    code: 25,
  }, `Should throw ${expectedErrorName} when cloning uncloneables`);
}

// markAsUncloneable cannot affect ArrayBuffer
{
  const pooledBuffer = new ArrayBuffer(8);
  const { port1, port2 } = new MessageChannel();
  markAsUncloneable(pooledBuffer);
  port1.postMessage(pooledBuffer);
  port2.on('message', mustCall((value) => {
    assert.deepStrictEqual(value, pooledBuffer);
    port2.close(mustCall());
  }));
}

// markAsUncloneable can affect Node.js built-in object like Blob
{
  const cloneableBlob = new Blob();
  const { port1, port2 } = new MessageChannel();
  port1.postMessage(cloneableBlob);
  port2.on('message', mustCall((value) => {
    assert.deepStrictEqual(value, cloneableBlob);
    port2.close(mustCall());
  }));

  const uncloneableBlob = new Blob();
  markAsUncloneable(uncloneableBlob);
  assert.throws(() => port1.postMessage(uncloneableBlob), {
    constructor: DOMException,
    name: expectedErrorName,
    code: 25,
  }, `Should throw ${expectedErrorName} when cloning uncloneables`);
}
