'use strict';
const common = require('../common');
const assert = require('assert');
const fs = require('fs');

// This test ensures that input for fchmod is valid, testing for valid
// inputs for fd and mode

// Check input type
[false, null, undefined, {}, [], ''].forEach((input) => {
  const errObj = {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError',
    message: 'The "fd" argument must be of type number.' +
             common.invalidArgTypeHelper(input)
  };
  assert.throws(() => fs.fchmod(input, 0o666, () => {}), errObj);
  assert.throws(() => fs.fchmodSync(input, 0o666), errObj);
});


[false, null, {}, []].forEach((input) => {
  const errObj = {
    code: 'ERR_INVALID_ARG_TYPE',
  };
  assert.throws(() => fs.fchmod(1, input), errObj);
  assert.throws(() => fs.fchmodSync(1, input), errObj);
});

assert.throws(() => fs.fchmod(1, '123x'), {
  code: 'ERR_INVALID_ARG_VALUE'
});

[-1, 2 ** 32].forEach((input) => {
  const errObj = {
    code: 'ERR_OUT_OF_RANGE',
    name: 'RangeError',
    message: 'The value of "fd" is out of range. It must be >= 0 && <= ' +
             `2147483647. Received ${input}`
  };
  assert.throws(() => fs.fchmod(input, 0o666, () => {}), errObj);
  assert.throws(() => fs.fchmodSync(input, 0o666), errObj);
});

[-1, 2 ** 32].forEach((input) => {
  const errObj = {
    code: 'ERR_OUT_OF_RANGE',
    name: 'RangeError',
    message: 'The value of "mode" is out of range. It must be >= 0 && <= ' +
             `4294967295. Received ${input}`
  };

  assert.throws(() => fs.fchmod(1, input, () => {}), errObj);
  assert.throws(() => fs.fchmodSync(1, input), errObj);
});

[NaN, Infinity].forEach((input) => {
  const errObj = {
    code: 'ERR_OUT_OF_RANGE',
    name: 'RangeError',
    message: 'The value of "fd" is out of range. It must be an integer. ' +
             `Received ${input}`
  };
  assert.throws(() => fs.fchmod(input, 0o666, () => {}), errObj);
  assert.throws(() => fs.fchmodSync(input, 0o666), errObj);
  errObj.message = errObj.message.replace('fd', 'mode');
  assert.throws(() => fs.fchmod(1, input, () => {}), errObj);
  assert.throws(() => fs.fchmodSync(1, input), errObj);
});

[1.5].forEach((input) => {
  const errObj = {
    code: 'ERR_OUT_OF_RANGE',
    name: 'RangeError',
    message: 'The value of "fd" is out of range. It must be an integer. ' +
             `Received ${input}`
  };
  assert.throws(() => fs.fchmod(input, 0o666, () => {}), errObj);
  assert.throws(() => fs.fchmodSync(input, 0o666), errObj);
  errObj.message = errObj.message.replace('fd', 'mode');
  assert.throws(() => fs.fchmod(1, input, () => {}), errObj);
  assert.throws(() => fs.fchmodSync(1, input), errObj);
});
