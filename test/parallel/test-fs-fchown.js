'use strict';

require('../common');
const assert = require('assert');
const fs = require('fs');

function testFd(input, errObj) {
  assert.throws(() => fs.fchown(input, 0, 0, () => {}), errObj);
  assert.throws(() => fs.fchownSync(input, 0, 0), errObj);
}

function testUid(input, errObj) {
  assert.throws(() => fs.fchown(1, input), errObj);
  assert.throws(() => fs.fchownSync(1, input), errObj);
}

function testGid(input, errObj) {
  assert.throws(() => fs.fchown(1, 1, input), errObj);
  assert.throws(() => fs.fchownSync(1, 1, input), errObj);
}

['', false, null, undefined, {}, []].forEach((input) => {
  const errObj = {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError',
    message: /fd|uid|gid/
  };
  testFd(input, errObj);
  testUid(input, errObj);
  testGid(input, errObj);
});

[Infinity, NaN].forEach((input) => {
  const errObj = {
    code: 'ERR_OUT_OF_RANGE',
    name: 'RangeError',
    message: 'The value of "fd" is out of range. It must be an integer. ' +
             `Received ${input}`
  };
  testFd(input, errObj);
  errObj.message = errObj.message.replace('fd', 'uid');
  testUid(input, errObj);
  errObj.message = errObj.message.replace('uid', 'gid');
  testGid(input, errObj);
});

[-2, 2 ** 32].forEach((input) => {
  const errObj = {
    code: 'ERR_OUT_OF_RANGE',
    name: 'RangeError',
    message: 'The value of "fd" is out of range. It must be ' +
             `>= 0 && <= 2147483647. Received ${input}`
  };
  testFd(input, errObj);
  errObj.message = 'The value of "uid" is out of range. It must be >= -1 && ' +
    `<= 4294967295. Received ${input}`;
  testUid(input, errObj);
  errObj.message = errObj.message.replace('uid', 'gid');
  testGid(input, errObj);
});
