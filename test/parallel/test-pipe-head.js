'use strict';
const common = require('../common');
const fixtures = require('../common/fixtures');
const assert = require('assert');

const exec = require('child_process').exec;

const script = fixtures.path('print-10-lines.js');

const cmd = `"${common.isWindows ? process.execPath : '$NODE'}" "${common.isWindows ? script : '$FILE'}" | head -2`;

exec(cmd, {
  env: common.isWindows ? process.env : { ...process.env, NODE: process.execPath, FILE: script },
}, common.mustSucceed((stdout, stderr) => {
  const lines = stdout.split('\n');
  assert.strictEqual(lines.length, 3);
}));
