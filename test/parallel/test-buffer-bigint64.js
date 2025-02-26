'use strict';
require('../common');
const assert = require('assert');

const buf = Buffer.allocUnsafe(9);

['LE', 'BE'].forEach(function(endianness) {
  // Should allow simple BigInts to be written and read
  let val = 123456789n;
  buf[`writeBigInt64${endianness}`](val, 0);
  let rtn = buf[`readBigInt64${endianness}`](0);
  assert.strictEqual(val, rtn);

  // Should allow INT64_MAX to be written and read
  val = 0x7fffffffffffffffn;
  buf[`writeBigInt64${endianness}`](val, 0);
  rtn = buf[`readBigInt64${endianness}`](0);
  assert.strictEqual(val, rtn);

  // Should read and write a negative signed 64-bit integer
  val = -123456789n;
  buf[`writeBigInt64${endianness}`](val, 0);
  assert.strictEqual(val, buf[`readBigInt64${endianness}`](0));

  // Should read and write an unsigned 64-bit integer
  val = 123456789n;
  buf[`writeBigUInt64${endianness}`](val, 0);
  assert.strictEqual(val, buf[`readBigUInt64${endianness}`](0));

  assert.strictEqual(buf[`writeBigUInt64${endianness}`](val, 0), 8);
  assert.strictEqual(buf[`writeBigInt64${endianness}`](val, 0), 8);
  assert.strictEqual(buf[`writeBigUInt64${endianness}`](val, 1), 9);
  assert.strictEqual(buf[`writeBigInt64${endianness}`](val, 1), 9);

  // Should throw a RangeError upon INT64_MAX+1 being written
  assert.throws(function() {
    const val = 0x8000000000000000n;
    buf[`writeBigInt64${endianness}`](val, 0);
  }, RangeError);

  // Should throw a RangeError upon UINT64_MAX+1 being written
  assert.throws(function() {
    const val = 0x10000000000000000n;
    buf[`writeBigUInt64${endianness}`](val, 0);
  }, {
    code: 'ERR_OUT_OF_RANGE',
    message: 'The value of "value" is out of range. It must be ' +
      '>= 0n and < 2n ** 64n. Received 18_446_744_073_709_551_616n'
  });

  // Should throw a TypeError upon invalid input
  assert.throws(function() {
    buf[`writeBigInt64${endianness}`]('bad', 0);
  }, TypeError);

  // Should throw a TypeError upon invalid input
  assert.throws(function() {
    buf[`writeBigUInt64${endianness}`]('bad', 0);
  }, TypeError);
});
