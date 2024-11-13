'use strict';
// Flags: --expose-gc

const common = require('../../common');
const test_finalizer = require(`./build/${common.buildType}/test_finalizer`);
const assert = require('assert');

const { gcUntil } = require('../../common/gc');

// The goal of this test is to show that we can run "pure" finalizers in the
// current JS loop tick. Thus, we do not use gcUntil function works
// asynchronously using micro tasks.
// We use IIFE for the obj scope instead of {} to be compatible with
// non-V8 JS engines that do not support scoped variables.
(() => {
  const obj = {};
  test_finalizer.addFinalizer(obj);
})();

for (let i = 0; i < 10; ++i) {
  global.gc();
  if (test_finalizer.getFinalizerCallCount() === 1) {
    break;
  }
}

assert.strictEqual(test_finalizer.getFinalizerCallCount(), 1);

// The finalizer that access JS cannot run synchronously. They are run in the
// next JS loop tick. Thus, we must use gcUntil.
async function runAsyncTests() {
  // We do not use common.mustCall() because we want to see the finalizer
  // called in response to GC and not as a part of env destruction.
  let js_is_called = false;
  // We use IIFE for the obj scope instead of {} to be compatible with
  // non-V8 JS engines that do not support scoped variables.
  (() => {
    const obj = {};
    test_finalizer.addFinalizerWithJS(obj, () => { js_is_called = true; });
  })();
  await gcUntil('ensure JS finalizer called',
                () => (test_finalizer.getFinalizerCallCount() === 2));
  assert(js_is_called);
}
runAsyncTests();
