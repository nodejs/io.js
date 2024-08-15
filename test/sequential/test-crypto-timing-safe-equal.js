// Flags: --expose-internals --no-warnings --allow-natives-syntax
'use strict';
const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const crypto = require('crypto');

// 'should consider equal strings to be equal'
assert.strictEqual(
  crypto.timingSafeEqual(Buffer.from('foo'), Buffer.from('foo')),
  true
);

// 'should consider unequal strings to be unequal'
assert.strictEqual(
  crypto.timingSafeEqual(Buffer.from('foo'), Buffer.from('bar')),
  false
);

{
  // Test TypedArrays with different lengths but equal byteLengths.
  const buf = crypto.randomBytes(16).buffer;
  const a1 = new Uint8Array(buf);
  const a2 = new Uint16Array(buf);
  const a3 = new Uint32Array(buf);

  for (const left of [a1, a2, a3]) {
    for (const right of [a1, a2, a3]) {
      assert.strictEqual(crypto.timingSafeEqual(left, right), true);
    }
  }
}

{
  // When the inputs are floating-point numbers, timingSafeEqual neither has
  // equality nor SameValue semantics. It just compares the underlying bytes,
  // ignoring the TypedArray type completely.

  const cmp = (fn) => (a, b) => a.every((x, i) => fn(x, b[i]));
  const eq = cmp((a, b) => a === b);
  const is = cmp(Object.is);

  function test(a, b, { equal, sameValue, timingSafeEqual }) {
    assert.strictEqual(eq(a, b), equal);
    assert.strictEqual(is(a, b), sameValue);
    assert.strictEqual(crypto.timingSafeEqual(a, b), timingSafeEqual);
  }

  test(new Float32Array([NaN]), new Float32Array([NaN]), {
    equal: false,
    sameValue: true,
    timingSafeEqual: true
  });

  test(new Float64Array([0]), new Float64Array([-0]), {
    equal: true,
    sameValue: false,
    timingSafeEqual: false
  });

  const x = new BigInt64Array([0x7ff0000000000001n, 0xfff0000000000001n]);
  test(new Float64Array(x.buffer), new Float64Array([NaN, NaN]), {
    equal: false,
    sameValue: true,
    timingSafeEqual: false
  });
}

assert.throws(
  () => crypto.timingSafeEqual(Buffer.from([1, 2, 3]), Buffer.from([1, 2])),
  {
    code: 'ERR_CRYPTO_TIMING_SAFE_EQUAL_LENGTH',
    name: 'RangeError',
    message: 'Input buffers must have the same byte length'
  }
);

assert.throws(
  () => crypto.timingSafeEqual('not a buffer', Buffer.from([1, 2])),
  {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError',
  }
);

assert.throws(
  () => crypto.timingSafeEqual(Buffer.from([1, 2]), 'not a buffer'),
  {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError',
  }
);

if (common.isDebug) {
  const { internalBinding } = require('internal/test/binding');
  const { getV8FastApiCallCount } = internalBinding('debug');

  const foo = Buffer.from('foo');
  const bar = Buffer.from('bar');
  const longer = Buffer.from('longer');
  function testFastPath(buf1, buf2) {
    return crypto.timingSafeEqual(buf1, buf2);
  }
  eval('%PrepareFunctionForOptimization(testFastPath)');
  assert.strictEqual(testFastPath(foo, bar), false);
  eval('%OptimizeFunctionOnNextCall(testFastPath)');
  assert.strictEqual(testFastPath(foo, bar), false);
  assert.strictEqual(testFastPath(foo, foo), true);
  assert.throws(() => testFastPath(foo, longer), {
    code: 'ERR_CRYPTO_TIMING_SAFE_EQUAL_LENGTH',
  });
  assert.strictEqual(getV8FastApiCallCount('crypto.timingSafeEqual.ok'), 2);
  assert.strictEqual(getV8FastApiCallCount('crypto.timingSafeEqual.error'), 1);
}
