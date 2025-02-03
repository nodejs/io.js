'use strict';

// Regression test for https://github.com/nodejs/node/issues/13262

const common = require('../common');
const assert = require('assert');
const async_hooks = require('async_hooks');
const { isMainThread } = require('worker_threads');

if (!isMainThread) {
  common.skip('Worker bootstrapping works differently -> different async IDs');
}

let seenId, seenResource;

async_hooks.createHook({
  init: common.mustCall((id, provider, triggerAsyncId, resource) => {
    seenId = id;
    seenResource = resource;
    assert.strictEqual(provider, 'Immediate');
    assert.strictEqual(triggerAsyncId, 1);
  }),
  before: common.mustNotCall(),
  after: common.mustNotCall(),
  destroy: common.mustCall((id) => {
    assert.strictEqual(seenId, id);
  })
}).enable();

const immediate = setImmediate(common.mustNotCall());
assert.strictEqual(immediate, seenResource);
clearImmediate(immediate);
