'use strict';
require('../common');
const assert = require('assert').strict;

assert.throws(() => {
  process.env.FORCE_COLOR = '1';
  delete process.env.NODE_DISABLE_COLORS;
  delete process.env.NO_COLOR;
  assert.deepStrictEqual([1, 2, 2, 2, 2], [2, 2, 2, 2, 2]);
}, (err) => {
  const expected = 'Expected values to be strictly deep-equal:\n' +
    '\x1B[32m+ actual\x1B[39m \x1B[31m- expected\x1B[39m\n' +
    '\n' +
    '\x1B[39m  [\n' +
    '\x1B[32m+\x1B[39m   1,\n' +
    '\x1B[39m    2,\n' +
    '\x1B[39m    2,\n' +
    '\x1B[39m    2,\n' +
    '\x1B[39m    2,\n' +
    '\x1B[31m-\x1B[39m   2\n' +
    '\x1B[39m  ]\n';
  assert.strictEqual(err.message, expected);
  return true;
});
