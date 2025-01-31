'use strict';

// Regression test for https://github.com/nodejs/node/issues/13237

const common = require('../common');
const assert = require('assert');
const { isMainThread } = require('worker_threads');

if (!isMainThread) {
  common.skip('Worker bootstrapping works differently -> different timing');
}

const async_hooks = require('async_hooks');

const seenEvents = [];

const p = new Promise((resolve) => resolve(1));
p.then(() => seenEvents.push('then'));

const hooks = async_hooks.createHook({
  init: common.mustNotCall(),

  before: common.mustCall((id) => {
    assert.ok(id > 1);
    seenEvents.push('before');
  }),

  after: common.mustCall((id) => {
    assert.ok(id > 1);
    seenEvents.push('after');
    hooks.disable();
  })
});

setImmediate(() => {
  assert.deepStrictEqual(seenEvents, ['before', 'then', 'after']);
});

hooks.enable(); // After `setImmediate` in order to not catch its init event.
