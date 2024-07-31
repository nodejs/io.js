'use strict';
require('../common');
const assert = require('assert');
const fs = require('fs');

for (const input of [Infinity, -Infinity, NaN]) {
  assert.throws(
    () => {
      fs._toUnixTimestamp(input);
    },
    {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError'
    });
}

assert.throws(
  () => {
    fs._toUnixTimestamp({});
  },
  {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError'
  });

const okInputs = [1, -1, '1', '-1', Date.now()];
for (const input of okInputs) {
  fs._toUnixTimestamp(input);
}
