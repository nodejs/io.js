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
const { getSystemErrorName } = require('util');
const spawn = require('child_process').spawn;
const assert = require('assert');
const fs = require('fs');

const enoentPath = 'foo123';
const spawnargs = ['bar'];
assert.strictEqual(fs.existsSync(enoentPath), false);

const enoentChild = spawn(enoentPath, spawnargs);

// Verify that stdio is setup if the error is not EMFILE or ENFILE.
assert.notStrictEqual(enoentChild.stdin, undefined);
assert.notStrictEqual(enoentChild.stdout, undefined);
assert.notStrictEqual(enoentChild.stderr, undefined);
assert(Array.isArray(enoentChild.stdio));
assert.strictEqual(enoentChild.stdio[0], enoentChild.stdin);
assert.strictEqual(enoentChild.stdio[1], enoentChild.stdout);
assert.strictEqual(enoentChild.stdio[2], enoentChild.stderr);

// Verify pid is not assigned.
assert.strictEqual(enoentChild.pid, undefined);

enoentChild.on('spawn', common.mustNotCall());

enoentChild.on('error', common.mustCall(function(err) {
  assert.strictEqual(err.code, 'ENOENT');
  assert.strictEqual(getSystemErrorName(err.errno), 'ENOENT');
  assert.strictEqual(err.syscall, `spawn ${enoentPath}`);
  assert.strictEqual(err.path, enoentPath);
  assert.deepStrictEqual(err.spawnargs, spawnargs);
}));

// Verify that the spawn() function throws when the file parameter is not a valid string
{
  assert.throws(() => {
    spawn(123, common.mustNotCall());
  }, { code: 'ERR_INVALID_ARG_TYPE' });

  assert.throws(() => {
    spawn('', common.mustNotCall());
  }, { code: 'ERR_INVALID_ARG_VALUE' });

  assert.throws(() => {
    spawn('\u0000', common.mustNotCall());
  }, { code: 'ERR_INVALID_ARG_VALUE' });
}
