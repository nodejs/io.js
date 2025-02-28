'use strict';
const common = require('../common');

// Buffer with size > INT32_MAX
common.skipIf32Bits();

// Test Buffer size larger than integer range
const { test } = require('node:test');
const assert = require('assert');
const {
  SlowBuffer,
} = require('buffer');
const kStringMaxLength = require('buffer').constants.MAX_STRING_LENGTH;

const stringTooLongError = {
  message: `Cannot create a string longer than 0x${kStringMaxLength.toString(16)}` +
    ' characters',
  code: 'ERR_STRING_TOO_LONG',
  name: 'Error',
};

const size = 2 ** 31;

// Test Buffer.toString
test('Buffer.toString with too long size', () => {
  try {
    assert.throws(() => SlowBuffer(size).toString('utf8'), stringTooLongError);
    assert.throws(() => Buffer.alloc(size).toString('utf8'), stringTooLongError);
    assert.throws(() => Buffer.allocUnsafe(size).toString('utf8'), stringTooLongError);
    assert.throws(() => Buffer.allocUnsafeSlow(size).toString('utf8'), stringTooLongError);
  } catch (e) {
    if (e.code !== 'ERR_MEMORY_ALLOCATION_FAILED') {
      throw e;
    }
    common.skip('insufficient space for Buffer.alloc');
  }
});

// Test Buffer.write
test('Buffer.write with too long size', () => {
  try {
    const buf = Buffer.alloc(size);
    assert.strictEqual(buf.write('a', 2, kStringMaxLength), 1);
    assert.strictEqual(buf.write('a', 2, size), 1);
  } catch (e) {
    if (e.code !== 'ERR_MEMORY_ALLOCATION_FAILED') {
      throw e;
    }
    common.skip('insufficient space for Buffer.alloc');
  }
});
