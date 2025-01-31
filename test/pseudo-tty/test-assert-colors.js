// Flags: --no-warnings
'use strict';
require('../common');
const assert = require('assert').strict;

function setup() {
  process.env.FORCE_COLOR = '1';
  delete process.env.NODE_DISABLE_COLORS;
  delete process.env.NO_COLOR;
}

assert.throws(() => {
  setup();
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

{
  assert.throws(() => {
    setup();
    assert.partialDeepStrictEqual({ a: 1, b: 2, c: 3, d: 5 }, { z: 4, b: 5 });
  }, (err) => {
    const expected = 'Expected values to be partially and strictly deep-equal:\n' +
      '\x1B[90mactual\x1B[39m \x1B[31m- expected\x1B[39m\n' +
      '\n' +
      '\x1B[39m  {\n' +
      '\x1B[90m    a: 1,\x1B[39m\n' +
      '\x1B[90m    b: 2,\x1B[39m\n' +
      '\x1B[90m    c: 3,\x1B[39m\n' +
      '\x1B[90m    d: 5\x1B[39m\n' +
      '\x1B[31m-\x1B[39m   b: 5,\n' +
      '\x1B[31m-\x1B[39m   z: 4\n' +
      '\x1B[39m  }\n';

    assert.strictEqual(err.message, expected);
    return true;
  });

  assert.throws(() => {
    setup();
    assert.partialDeepStrictEqual([1, 2, 3, 5], [4, 5]);
  }, (err) => {
    const expected = 'Expected values to be partially and strictly deep-equal:\n' +
      '\x1B[90mactual\x1B[39m \x1B[31m- expected\x1B[39m\n' +
      '\n' +
      '\x1B[39m  [\n' +
      '\x1B[90m    1,\x1B[39m\n' +
      '\x1B[90m    2,\x1B[39m\n' +
      '\x1B[90m    3,\x1B[39m\n' +
      '\x1B[31m-\x1B[39m   4,\n' +
      '\x1B[39m    5\n' +
      '\x1B[39m  ]\n';

    assert.strictEqual(err.message, expected);
    return true;
  });
}
