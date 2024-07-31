'use strict';
const common = require('../common');
const assert = require('assert');
const fs = require('fs');

const prefixValues = [undefined, null, 0, true, false, 1];

function fail(value) {
  assert.throws(
    () => {
      fs.mkdtempSync(value, {});
    },
    {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError'
    });
}

function failAsync(value) {
  assert.throws(
    () => {
      fs.mkdtemp(value, common.mustNotCall());
    },
    {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError'
    });
}

for (const prefixValue of prefixValues) {
  fail(prefixValue);
  failAsync(prefixValue);
}
