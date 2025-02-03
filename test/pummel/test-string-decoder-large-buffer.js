'use strict';
const common = require('../common');

// Buffer with size > INT32_MAX
common.skipIf32Bits();

const assert = require('assert');
const { StringDecoder } = require('node:string_decoder');
const kStringMaxLength = require('buffer').constants.MAX_STRING_LENGTH;

const size = 2 ** 31;

const stringTooLongError = {
  message: `Cannot create a string longer than 0x${kStringMaxLength.toString(16)}` +
    ' characters',
  code: 'ERR_STRING_TOO_LONG',
  name: 'Error',
};

try {
  const buf = Buffer.allocUnsafe(size);
  const decoder = new StringDecoder('utf8');
  assert.throws(() => decoder.write(buf), stringTooLongError);
} catch (e) {
  if (e.code !== 'ERR_MEMORY_ALLOCATION_FAILED') {
    throw e;
  }
  common.skip('insufficient space for Buffer.alloc');
}
