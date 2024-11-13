// Flags: --expose-internals
'use strict';

const common = require('../common');
const assert = require('assert');
const {
  getSystemErrorName,
  getSystemErrorMessage,
  _errnoException
} = require('util');

const { internalBinding } = require('internal/test/binding');
const uv = internalBinding('uv');
const keys = Object.keys(uv);

assert.strictEqual(uv.errname(-111111), 'Unknown system error -111111');
assert.strictEqual(uv.getErrorMessage(-111111), 'Unknown system error -111111');

keys.forEach((key) => {
  if (!key.startsWith('UV_'))
    return;

  const err = _errnoException(uv[key], 'test');
  const name = uv.errname(uv[key]);
  assert.strictEqual(getSystemErrorName(uv[key]), name);
  assert.notStrictEqual(getSystemErrorMessage(uv[key]),
                        `Unknown system error ${key}`);
  assert.strictEqual(err.code, name);
  assert.strictEqual(err.code, getSystemErrorName(err.errno));
  assert.strictEqual(err.message, `test ${name}`);
});

function runTest(fn) {
  ['test', {}, []].forEach((err) => {
    assert.throws(
      () => fn(err),
      {
        code: 'ERR_INVALID_ARG_TYPE',
        name: 'TypeError',
        message: 'The "err" argument must be of type number.' +
                 common.invalidArgTypeHelper(err)
      });
  });

  [0, 1, Infinity, -Infinity, NaN].forEach((err) => {
    assert.throws(
      () => fn(err),
      {
        code: 'ERR_OUT_OF_RANGE',
        name: 'RangeError',
        message: 'The value of "err" is out of range. ' +
                 'It must be a negative integer. ' +
                 `Received ${err}`
      });
  });
}

runTest(_errnoException);
runTest(getSystemErrorName);
runTest(getSystemErrorMessage);
