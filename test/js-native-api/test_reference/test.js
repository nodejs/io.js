'use strict';
// Flags: --expose-gc

const { buildType } = require('../../common');
const { gcUntil } = require('../../common/gc');
const assert = require('assert');

const test_reference = require(`./build/${buildType}/test_reference`);

// This test script uses external values with finalizer callbacks
// in order to track when values get garbage-collected. Each invocation
// of a finalizer callback increments the finalizeCount property.
assert.strictEqual(test_reference.finalizeCount, 0);

// Run each test function in sequence,
// with an async delay and GC call between each.
async function runTests() {
  (() => {
    const symbol = test_reference.createSymbol('testSym');
    test_reference.createReference(symbol, 0);
    assert.strictEqual(test_reference.referenceValue, symbol);
  })();
  test_reference.deleteReference();

  (() => {
    const symbol = test_reference.createSymbolFor('testSymFor');
    test_reference.createReference(symbol, 0);
    assert.strictEqual(test_reference.referenceValue, symbol);
  })();
  test_reference.deleteReference();

  (() => {
    const symbol = test_reference.createSymbolFor('testSymFor');
    test_reference.createReference(symbol, 1);
    assert.strictEqual(test_reference.referenceValue, symbol);
    assert.strictEqual(test_reference.referenceValue, Symbol.for('testSymFor'));
  })();
  test_reference.deleteReference();

  (() => {
    const symbol = test_reference.createSymbolForEmptyString();
    test_reference.createReference(symbol, 0);
    assert.strictEqual(test_reference.referenceValue, Symbol.for(''));
  })();
  test_reference.deleteReference();

  (() => {
    const symbol = test_reference.createSymbolForEmptyString();
    test_reference.createReference(symbol, 1);
    assert.strictEqual(test_reference.referenceValue, symbol);
    assert.strictEqual(test_reference.referenceValue, Symbol.for(''));
  })();
  test_reference.deleteReference();

  assert.throws(() => test_reference.createSymbolForIncorrectLength(),
                /Invalid argument/);

  (() => {
    const value = test_reference.createExternal();
    assert.strictEqual(test_reference.finalizeCount, 0);
    assert.strictEqual(typeof value, 'object');
    test_reference.checkExternal(value);
  })();
  await gcUntil('External value without a finalizer',
                () => (test_reference.finalizeCount === 0));

  (() => {
    const value = test_reference.createExternalWithFinalize();
    assert.strictEqual(test_reference.finalizeCount, 0);
    assert.strictEqual(typeof value, 'object');
    test_reference.checkExternal(value);
  })();
  await gcUntil('External value with a finalizer',
                () => (test_reference.finalizeCount === 1));

  (() => {
    const value = test_reference.createExternalWithFinalize();
    assert.strictEqual(test_reference.finalizeCount, 0);
    test_reference.createReference(value, 0);
    assert.strictEqual(test_reference.referenceValue, value);
  })();
  // Value should be GC'd because there is only a weak ref
  await gcUntil('Weak reference',
                () => (test_reference.referenceValue === undefined &&
                test_reference.finalizeCount === 1));
  test_reference.deleteReference();

  (() => {
    const value = test_reference.createExternalWithFinalize();
    assert.strictEqual(test_reference.finalizeCount, 0);
    test_reference.createReference(value, 1);
    assert.strictEqual(test_reference.referenceValue, value);
  })();
  // Value should NOT be GC'd because there is a strong ref
  await gcUntil('Strong reference',
                () => (test_reference.finalizeCount === 0));
  test_reference.deleteReference();
  await gcUntil('Strong reference (cont.d)',
                () => (test_reference.finalizeCount === 1));

  (() => {
    const value = test_reference.createExternalWithFinalize();
    assert.strictEqual(test_reference.finalizeCount, 0);
    test_reference.createReference(value, 1);
  })();
  // Value should NOT be GC'd because there is a strong ref
  await gcUntil('Strong reference, increment then decrement to weak reference',
                () => (test_reference.finalizeCount === 0));
  assert.strictEqual(test_reference.incrementRefcount(), 2);
  // Value should NOT be GC'd because there is a strong ref
  await gcUntil(
    'Strong reference, increment then decrement to weak reference (cont.d-1)',
    () => (test_reference.finalizeCount === 0));
  assert.strictEqual(test_reference.decrementRefcount(), 1);
  // Value should NOT be GC'd because there is a strong ref
  await gcUntil(
    'Strong reference, increment then decrement to weak reference (cont.d-2)',
    () => (test_reference.finalizeCount === 0));
  assert.strictEqual(test_reference.decrementRefcount(), 0);
  // Value should be GC'd because the ref is now weak!
  await gcUntil(
    'Strong reference, increment then decrement to weak reference (cont.d-3)',
    () => (test_reference.finalizeCount === 1));
  test_reference.deleteReference();
  // Value was already GC'd
  await gcUntil(
    'Strong reference, increment then decrement to weak reference (cont.d-4)',
    () => (test_reference.finalizeCount === 1));
}
runTests();

// This test creates a napi_ref on an object that has
// been wrapped by napi_wrap and for which the finalizer
// for the wrap calls napi_delete_ref on that napi_ref.
//
// Since both the wrap and the reference use the same
// object the finalizer for the wrap and reference
// may run in the same gc and in any order.
//
// It does that to validate that napi_delete_ref can be
// called before the finalizer has been run for the
// reference (there is a finalizer behind the scenes even
// though it cannot be passed to napi_create_reference).
//
// Since the order is not guaranteed, run the
// test a number of times maximize the chance that we
// get a run with the desired order for the test.
//
// 1000 reliably recreated the problem without the fix
// required to ensure delete could be called before
// the finalizer in manual testing.
for (let i = 0; i < 1000; i++) {
  const wrapObject = new Object();
  test_reference.validateDeleteBeforeFinalize(wrapObject);
  global.gc();
}
