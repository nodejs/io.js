'use strict';
const common = require('../common');

// Simulate `cat readfile.js | node readfile.js`

if (common.isWindows || common.isAIX || common.isIBMi)
  common.skip(`No /dev/stdin on ${process.platform}.`);

const assert = require('assert');
const fs = require('fs');

if (process.argv[2] === 'child') {
  process.stdout.write(fs.readFileSync('/dev/stdin', 'utf8'));
  return;
}

const tmpdir = require('../common/tmpdir');

const filename = tmpdir.resolve('readfilesync_pipe_large_test.txt');
const dataExpected = 'a'.repeat(999999);
tmpdir.refresh();
fs.writeFileSync(filename, dataExpected);

const exec = require('child_process').exec;
const [cmd, opts] = common.escapePOSIXShell`"${process.execPath}" "${__filename}" child < "${filename}"`;
exec(
  cmd,
  { ...opts, maxBuffer: 1_000_000 },
  common.mustSucceed((stdout, stderr) => {
    assert.strictEqual(stdout, dataExpected);
    assert.strictEqual(stderr, '');
    console.log('ok');
  })
);
