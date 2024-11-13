'use strict';
const common = require('../common');
const assert = require('assert');
const spawn = require('child_process').spawn;
const fs = require('fs');
const fixtures = require('../common/fixtures');

if (common.isWindows) {
  if (process.argv[2] === 'child') {
    /* eslint-disable no-unused-expressions */
    process.stdin;
    process.stdout;
    process.stderr;
    return;
    /* eslint-enable no-unused-expressions */
  }
  const python = process.env.PYTHON || 'python';
  const script = fixtures.path('spawn_closed_stdio.py');
  const proc = spawn(python, [script, process.execPath, __filename, 'child']);
  proc.on('exit', common.mustCall(function(exitCode) {
    assert.strictEqual(exitCode, 0);
  }));
  return;
}

if (process.argv[2] === 'child') {
  [0, 1, 2].forEach((i) => fs.fstatSync(i));
  return;
}

// Run the script in a shell but close stdout and stderr.
const [cmd, opts] = common.escapePOSIXShell`"${process.execPath}" "${__filename}" child 1>&- 2>&-`;
const proc = spawn('/bin/sh', ['-c', cmd], { ...opts, stdio: 'inherit' });

proc.on('exit', common.mustCall(function(exitCode) {
  assert.strictEqual(exitCode, 0);
}));
