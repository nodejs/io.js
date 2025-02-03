'use strict';

const common = require('../common');
const assert = require('assert');
const { readdir, readdirSync } = require('fs');

if (!common.isWindows) {
  common.skip('This test is specific to Windows to test enumerate pipes');
}

// Ref: https://github.com/nodejs/node/issues/56002
// This test is specific to Windows.

const pipe = '\\\\.\\pipe\\';

const { length } = readdirSync(pipe);
assert.ok(length >= 0, `${length} is not greater or equal to 0`);

readdir(pipe, common.mustSucceed((files) => {
  assert.ok(files.length >= 0, `${files.length} is not greater or equal to 0`);
}));
