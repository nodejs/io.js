'use strict';
const common = require('../common');
const assert = require('assert');
const { AsyncLocalStorage } = require('async_hooks');

const als = new AsyncLocalStorage();

// The _propagate function only exists on the old JavaScript implementation.
if (typeof als._propagate === 'function') {
  // The als instance should be getting removed from the storageList in
  // lib/async_hooks.js when exit(...) is called, therefore when the nested runs
  // are called there should be no copy of the als in the storageList to run the
  // _propagate method on.
  als._propagate = common.mustNotCall('_propagate() should not be called');
}

const done = common.mustCall();

const data = true;

function run(count) {
  if (count === 0) return done();
  assert.notStrictEqual(als.getStore(), data);
  als.run(data, () => {
    als.exit(run, --count);
  });
}
run(100);
