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
require('../common');
const assert = require('assert');
const fs = require('fs');
const fixtures = require('../common/fixtures');
const tmpdir = require('../common/tmpdir');

const fn = fixtures.path('elipses.txt');
tmpdir.refresh();

const s = fs.readFileSync(fn, 'utf8');
for (let i = 0; i < s.length; i++) {
  assert.strictEqual(s[i], '\u2026');
}
assert.strictEqual(s.length, 10000);

// Test file permissions set for readFileSync() in append mode.
{
  const expectedMode = 0o666 & ~process.umask();

  for (const test of [
    { },
    { encoding: 'ascii' },
    { encoding: 'base64' },
    { encoding: 'hex' },
    { encoding: 'latin1' },
    { encoding: 'uTf8' }, // case variation
    { encoding: 'utf16le' },
    { encoding: 'utf8' },
  ]) {
    const opts = { ...test, flag: 'a+' };
    const file = tmpdir.resolve(`testReadFileSyncAppend${opts.encoding ?? ''}.txt`);
    const variant = `for '${file}'`;

    const content = fs.readFileSync(file, opts);
    assert.strictEqual(opts.encoding ? content : content.toString(), '', `file contents ${variant}`);
    assert.strictEqual(fs.statSync(file).mode & 0o777, expectedMode, `file permissions ${variant}`);
  }
}
