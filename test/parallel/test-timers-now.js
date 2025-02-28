// Flags: --expose-internals --no-warnings --allow-natives-syntax
'use strict';

const common = require('../common');
const assert = require('node:assert');
const { internalBinding } = require('internal/test/binding');
const binding = internalBinding('timers');

// Return value of getLibuvNow() should easily fit in a SMI after start-up.
// We need to use the binding as the receiver for fast API calls.
assert(binding.getLibuvNow() < 0x3ffffff);

{
  // V8 Fast API
  function optimized() { // eslint-disable-line no-unused-vars
    return binding.getLibuvNow();
  }

  function testFastPaths() {
    assert(binding.getLibuvNow() < 0x3ffffff);
  }

  eval('%PrepareFunctionForOptimization(optimized)');
  testFastPaths();
  eval('%OptimizeFunctionOnNextCall(optimized)');
  testFastPaths();

  if (common.isDebug) {
    const { getV8FastApiCallCount } = internalBinding('debug');
    assert.strictEqual(getV8FastApiCallCount('timers.getLibuvNow'), 1);
  }
}
