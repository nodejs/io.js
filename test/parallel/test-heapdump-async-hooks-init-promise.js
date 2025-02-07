// Flags: --expose-gc
'use strict';
const common = require('../common');
const assert = require('assert');
const async_hooks = require('async_hooks');
const v8 = require('v8');

// Regression test for https://github.com/nodejs/node/issues/28786
// Make sure that creating a heap snapshot inside an async_hooks hook
// works for Promises.

const createSnapshot = common.mustCall(() => {
  v8.getHeapSnapshot().resume();
}, 8);  // 2 × init + 2 × resolve + 1 × (after + before) + 2 × destroy = 8 calls

const promiseIds = [];

async_hooks.createHook({
  init(id, type) {
    if (type === 'PROMISE') {
      createSnapshot();
      promiseIds.push(id);
    }
  },

  before(id) {
    if (promiseIds.includes(id)) createSnapshot();
  },

  after(id) {
    if (promiseIds.includes(id)) createSnapshot();
  },

  promiseResolve(id) {
    assert(promiseIds.includes(id));
    createSnapshot();
  },

  destroy(id) {
    if (promiseIds.includes(id)) createSnapshot();
  }
}).enable();


Promise.resolve().then(() => {});
setImmediate(globalThis.gc);
