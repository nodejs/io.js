// Flags: --expose-internals --no-warnings --allow-natives-syntax
'use strict';
const common = require('../common');

const assert = require('assert');
const { internalBinding } = require('internal/test/binding');

if (!common.isDebug) {
  assert.throws(() => internalBinding('debug'), {
    message: 'No such binding: debug'
  });
  return;
}

const {
  getV8FastApiCallCount,
  isEven,
  isOdd,
} = internalBinding('debug');

assert.throws(() => getV8FastApiCallCount(), {
  message: 'getV8FastApiCallCount must be called with a string',
});

function testIsEven() {
  for (let i = 0; i < 10; i++) {
    assert.strictEqual(isEven(i), i % 2 === 0);
  }
}

function testIsOdd() {
  for (let i = 0; i < 20; i++) {
    assert.strictEqual(isOdd(i), i % 2 !== 0);
  }
}

// Should return 0 by default for any string.
assert.strictEqual(getV8FastApiCallCount(''), 0);
assert.strictEqual(getV8FastApiCallCount('foo'), 0);
assert.strictEqual(getV8FastApiCallCount('debug.isEven'), 0);
assert.strictEqual(getV8FastApiCallCount('debug.isOdd'), 0);

eval('%PrepareFunctionForOptimization(testIsEven)');
testIsEven();
eval('%PrepareFunctionForOptimization(testIsOdd)');
testIsOdd();

// Functions should not be optimized yet.
assert.strictEqual(getV8FastApiCallCount('debug.isEven'), 0);
assert.strictEqual(getV8FastApiCallCount('debug.isOdd'), 0);

eval('%OptimizeFunctionOnNextCall(testIsEven)');
testIsEven();
eval('%OptimizeFunctionOnNextCall(testIsOdd)');
testIsOdd();

// Functions should have been optimized and fast path taken.
assert.strictEqual(getV8FastApiCallCount('debug.isEven'), 10);
assert.strictEqual(getV8FastApiCallCount('debug.isOdd'), 20);
