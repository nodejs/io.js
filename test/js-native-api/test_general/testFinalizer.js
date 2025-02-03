'use strict';
// Flags: --expose-gc

const common = require('../../common');
const test_general = require(`./build/${common.buildType}/test_general`);
const assert = require('assert');
const { gcUntil } = require('../../common/gc');

let finalized = {};
const callback = common.mustCall(2);

// Add two items to be finalized and ensure the callback is called for each.
test_general.addFinalizerOnly(finalized, callback);
test_general.addFinalizerOnly(finalized, callback);

// Ensure attached items cannot be retrieved.
assert.throws(() => test_general.unwrap(finalized),
              { name: 'Error', message: 'Invalid argument' });

// Ensure attached items cannot be removed.
assert.throws(() => test_general.removeWrap(finalized),
              { name: 'Error', message: 'Invalid argument' });
finalized = null;
global.gc();

// Add an item to an object that is already wrapped, and ensure that its
// finalizer as well as the wrap finalizer gets called.
async function testFinalizeAndWrap() {
  assert.strictEqual(test_general.derefItemWasCalled(), false);
  let finalizeAndWrap = {};
  test_general.wrap(finalizeAndWrap);
  test_general.addFinalizerOnly(finalizeAndWrap, common.mustCall());
  finalizeAndWrap = null;
  await gcUntil('test finalize and wrap',
                () => test_general.derefItemWasCalled());
}
testFinalizeAndWrap();
