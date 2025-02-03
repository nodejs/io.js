// Flags: --expose-internals --no-warnings --allow-natives-syntax
'use strict';

const common = require('../common');

const { URL } = require('url');
const assert = require('assert');

const { internalBinding } = require('internal/test/binding');

// One argument is required
assert.throws(() => {
  URL.canParse();
}, {
  code: 'ERR_MISSING_ARGS',
  name: 'TypeError',
});

// It should not throw when called without a base string
assert.strictEqual(URL.canParse('https://example.org'), true);

{
  // V8 Fast API
  function testFastPaths() {
    // `canParse` binding has two overloads.
    assert.strictEqual(URL.canParse('https://www.example.com/path/?query=param#hash'), true);
    assert.strictEqual(URL.canParse('/', 'http://n'), true);
  }

  eval('%PrepareFunctionForOptimization(URL.canParse)');
  testFastPaths();
  eval('%OptimizeFunctionOnNextCall(URL.canParse)');
  testFastPaths();

  if (common.isDebug) {
    const { getV8FastApiCallCount } = internalBinding('debug');
    // TODO: the counts should be 1. The function is optimized, but the fast
    // API is not called.
    assert.strictEqual(getV8FastApiCallCount('url.canParse'), 0);
    assert.strictEqual(getV8FastApiCallCount('url.canParse.withBase'), 0);
  }
}
