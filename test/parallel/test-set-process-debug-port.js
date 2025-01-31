'use strict';
const common = require('../common');

common.skipIfInspectorDisabled();
const { isMainThread } = require('worker_threads');

if (!isMainThread) {
  common.skip('This test only works on a main thread');
}

const assert = require('assert');
const kMinPort = 1024;
const kMaxPort = 65535;

function check(value, expected) {
  process.debugPort = value;
  assert.strictEqual(process.debugPort, expected);
}

// Expected usage with numbers.
check(0, 0);
check(kMinPort, kMinPort);
check(kMinPort + 1, kMinPort + 1);
check(kMaxPort - 1, kMaxPort - 1);
check(kMaxPort, kMaxPort);

// Numeric strings coerce.
check('0', 0);
check(`${kMinPort}`, kMinPort);
check(`${kMinPort + 1}`, kMinPort + 1);
check(`${kMaxPort - 1}`, kMaxPort - 1);
check(`${kMaxPort}`, kMaxPort);

// Most other values are coerced to 0.
check('', 0);
check(false, 0);
check(NaN, 0);
check(Infinity, 0);
check(-Infinity, 0);
check(function() {}, 0);
check({}, 0);
check([], 0);

// Symbols do not coerce.
assert.throws(() => {
  process.debugPort = Symbol();
}, /^TypeError: Cannot convert a Symbol value to a number$/);

// Verify port bounds checking.
[
  true,
  -1,
  1,
  kMinPort - 1,
  kMaxPort + 1,
  '-1',
  '1',
  `${kMinPort - 1}`,
  `${kMaxPort + 1}`,
].forEach((value) => {
  assert.throws(() => {
    process.debugPort = value;
  }, /^RangeError: process\.debugPort must be 0 or in range 1024 to 65535$/);
});
