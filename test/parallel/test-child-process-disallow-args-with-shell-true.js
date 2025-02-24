'use strict';

// This test ensures that execFile and spawn do not take any arguments when
// shell option is set to true, as this can lead to bugs and security issues.
// See https://github.com/nodejs/node/issues/57143

require('../common');
const assert = require('assert');
const { execFile, execFileSync, spawn, spawnSync } = require('child_process');

const args = ['echo "hello', ['world"'], { shell: true }];
const err = { code: 'ERR_INVALID_ARG_VALUE' };

assert.throws(() => execFileSync(...args), err);
assert.throws(() => spawnSync(...args), err);
assert.throws(() => execFile(...args), err);
assert.throws(() => spawn(...args), err);
