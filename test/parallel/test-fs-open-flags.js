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

// Flags: --expose-internals
'use strict';
const common = require('../common');

const fixtures = require('../common/fixtures');

const assert = require('assert');
const fs = require('fs');

// 0 if not found in fs.constants
const { O_APPEND = 0,
        O_CREAT = 0,
        O_EXCL = 0,
        O_RDONLY = 0,
        O_RDWR = 0,
        O_SYNC = 0,
        O_DSYNC = 0,
        O_TRUNC = 0,
        O_WRONLY = 0 } = fs.constants;

const { stringToFlags } = require('internal/fs/utils');

assert.strictEqual(stringToFlags('r'), O_RDONLY);
assert.strictEqual(stringToFlags('r+'), O_RDWR);
assert.strictEqual(stringToFlags('rs+'), O_RDWR | O_SYNC);
assert.strictEqual(stringToFlags('sr+'), O_RDWR | O_SYNC);
assert.strictEqual(stringToFlags('w'), O_TRUNC | O_CREAT | O_WRONLY);
assert.strictEqual(stringToFlags('w+'), O_TRUNC | O_CREAT | O_RDWR);
assert.strictEqual(stringToFlags('a'), O_APPEND | O_CREAT | O_WRONLY);
assert.strictEqual(stringToFlags('a+'), O_APPEND | O_CREAT | O_RDWR);

assert.strictEqual(stringToFlags('wx'), O_TRUNC | O_CREAT | O_WRONLY | O_EXCL);
assert.strictEqual(stringToFlags('xw'), O_TRUNC | O_CREAT | O_WRONLY | O_EXCL);
assert.strictEqual(stringToFlags('wx+'), O_TRUNC | O_CREAT | O_RDWR | O_EXCL);
assert.strictEqual(stringToFlags('xw+'), O_TRUNC | O_CREAT | O_RDWR | O_EXCL);
assert.strictEqual(stringToFlags('ax'), O_APPEND | O_CREAT | O_WRONLY | O_EXCL);
assert.strictEqual(stringToFlags('xa'), O_APPEND | O_CREAT | O_WRONLY | O_EXCL);
assert.strictEqual(stringToFlags('as'), O_APPEND | O_CREAT | O_WRONLY | O_SYNC);
assert.strictEqual(stringToFlags('sa'), O_APPEND | O_CREAT | O_WRONLY | O_SYNC);
assert.strictEqual(stringToFlags('ax+'), O_APPEND | O_CREAT | O_RDWR | O_EXCL);
assert.strictEqual(stringToFlags('xa+'), O_APPEND | O_CREAT | O_RDWR | O_EXCL);
assert.strictEqual(stringToFlags('as+'), O_APPEND | O_CREAT | O_RDWR | O_SYNC);
assert.strictEqual(stringToFlags('sa+'), O_APPEND | O_CREAT | O_RDWR | O_SYNC);

('+ +a +r +w rw wa war raw r++ a++ w++ x +x x+ rx rx+ wxx wax xwx xxx')
  .split(' ')
  .forEach(function(flags) {
    assert.throws(
      () => stringToFlags(flags),
      { code: 'ERR_INVALID_ARG_VALUE', name: 'TypeError' }
    );
  });

assert.throws(
  () => stringToFlags({}),
  { code: 'ERR_INVALID_ARG_VALUE', name: 'TypeError' }
);

assert.throws(
  () => stringToFlags(true),
  { code: 'ERR_INVALID_ARG_VALUE', name: 'TypeError' }
);

if (common.isLinux || common.isMacOS) {
  const tmpdir = require('../common/tmpdir');
  tmpdir.refresh();
  const file = tmpdir.resolve('a.js');
  fs.copyFileSync(fixtures.path('a.js'), file);
  fs.open(file, O_DSYNC, common.mustSucceed((fd) => {
    fs.closeSync(fd);
  }));
}
