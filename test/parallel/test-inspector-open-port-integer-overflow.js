'use strict';

// Regression test for an integer overflow in inspector.open() when the port
// exceeds the range of an unsigned 16-bit integer.

const common = require('../common');
common.skipIfInspectorDisabled();

const { isMainThread } = require('worker_threads');

if (!isMainThread) {
  common.skip('This test only works on a main thread');
}

const assert = require('assert');
const inspector = require('inspector');

assert.throws(() => inspector.open(99999), {
  name: 'RangeError',
  code: 'ERR_OUT_OF_RANGE',
  message: 'The value of "port" is out of range. It must be >= 0 && <= 65535. Received 99999'
});
