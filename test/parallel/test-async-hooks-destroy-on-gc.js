'use strict';
// Flags: --expose_gc

// This test ensures that userland-only AsyncResources cause a destroy event to
// be emitted when they get gced.

const common = require('../common');
const assert = require('assert');
const async_hooks = require('async_hooks');

const destroyedIds = new Set();
async_hooks.createHook({
  destroy: common.mustCallAtLeast((asyncId) => {
    destroyedIds.add(asyncId);
  }, 1)
}).enable();

let asyncId = null;
{
  const res = new async_hooks.AsyncResource('foobar');
  asyncId = res.asyncId();
}

setImmediate(() => {
  globalThis.gc();
  setImmediate(() => assert.ok(destroyedIds.has(asyncId)));
});
