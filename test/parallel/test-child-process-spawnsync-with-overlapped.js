'use strict';
require('../common');
const assert = require('assert');
const { spawnSync, execFileSync, execSync } = require('child_process');

// See https://github.com/nodejs/node/issues/52265
const args = { stdio: ['pipe', 'overlapped', 'pipe'] };

assert.throws(() => {
  spawnSync(process.execPath, ['--version'], args);
}, /ERR_INVALID_ARG_VALUE/);

assert.throws(() => {
  execFileSync(process.execPath, ['--version'], args);
}, /ERR_INVALID_ARG_VALUE/);

assert.throws(() => {
  execSync(`${process.execPath} --version`, args);
}, /ERR_INVALID_ARG_VALUE/);
