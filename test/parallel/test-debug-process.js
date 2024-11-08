'use strict';
const common = require('../common');
const assert = require('assert');
const child = require('child_process');

if (!common.isWindows) {
  common.skip('This test is specific to Windows to test winapi_strerror');
}

// Ref: https://github.com/nodejs/node/issues/23191
// This test is specific to Windows.

const cp = child.spawn('pwd');

cp.on('exit', common.mustCall(function() {
  try {
    process._debugProcess(cp.pid);
  } catch (error) {
    assert.strictEqual(error.message, 'The system cannot find the file specified.');
  }
}));
