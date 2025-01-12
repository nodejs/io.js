'use strict';

const { invalidArgTypeHelper } = require('../common');

const assert = require('node:assert');
const zlib = require('node:zlib');
const { test } = require('node:test');

// Check the error condition testing for passing something other than a string
// or buffer.
test('check zlib for not string or buffer inputs', async (t) => {
  [
    undefined,
    null,
    true,
    false,
    0,
    1,
    [1, 2, 3],
    { foo: 'bar' },
  ].forEach((input) => {
    assert.throws(
      () => zlib.deflateSync(input),
      {
        code: 'ERR_INVALID_ARG_TYPE',
        name: 'TypeError',
        message: 'The "buffer" argument must be of type string or an instance ' +
                 'of Buffer, TypedArray, DataView, or ArrayBuffer.' +
                 invalidArgTypeHelper(input)
      }
    );
  });
});
