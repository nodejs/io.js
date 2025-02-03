// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

'use strict';
const common = require('../common');
if (common.isWindows)
  common.skip('no RLIMIT_NOFILE on Windows');

const assert = require('assert');
const child_process = require('child_process');
const fs = require('fs');

const ulimit = Number(child_process.execSync('ulimit -Hn'));
if (ulimit > 64 || Number.isNaN(ulimit)) {
  const [cmd, opts] = common.escapePOSIXShell`ulimit -n 64 && "${process.execPath}" "${__filename}"`;
  // Sorry about this nonsense. It can be replaced if
  // https://github.com/nodejs/node-v0.x-archive/pull/2143#issuecomment-2847886
  // ever happens.
  const result = child_process.spawnSync(
    '/bin/sh',
    ['-c', cmd],
    opts,
  );
  assert.strictEqual(result.stdout.toString(), '');
  assert.strictEqual(result.stderr.toString(), '');
  assert.strictEqual(result.status, 0);
  assert.strictEqual(result.error, undefined);
  return;
}

const openFds = [];

for (;;) {
  try {
    openFds.push(fs.openSync(__filename, 'r'));
  } catch (err) {
    assert.strictEqual(err.code, 'EMFILE');
    break;
  }
}

// Should emit an error, not throw.
const proc = child_process.spawn(process.execPath, ['-e', '0']);

// Verify that stdio is not setup on EMFILE or ENFILE.
assert.strictEqual(proc.stdin, undefined);
assert.strictEqual(proc.stdout, undefined);
assert.strictEqual(proc.stderr, undefined);
assert.strictEqual(proc.stdio, undefined);

proc.on('error', common.mustCall(function(err) {
  assert.strictEqual(err.code, 'EMFILE');
}));

proc.on('exit', common.mustNotCall('"exit" event should not be emitted'));

// Close one fd for LSan
if (openFds.length >= 1) {
  fs.closeSync(openFds.pop());
}
