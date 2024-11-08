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
const fs = require('fs');

// Test that fs.readFile fails correctly on a non-existent file.

// `fs.readFile('/')` does not fail on AIX and FreeBSD because you can open
// and read the directory there.
if (common.isAIX || common.isFreeBSD)
  common.skip('platform not supported.');

const assert = require('assert');
const exec = require('child_process').exec;
const fixtures = require('../common/fixtures');

function test(env, cb) {
  const filename = fixtures.path('test-fs-readfile-error.js');
  exec(...common.escapePOSIXShell`"${process.execPath}" "${filename}"`, (err, stdout, stderr) => {
    assert(err);
    assert.strictEqual(stdout, '');
    assert.notStrictEqual(stderr, '');
    cb(String(stderr));
  });
}

test({ NODE_DEBUG: '' }, common.mustCall((data) => {
  assert.match(data, /EISDIR/);
  assert.match(data, /test-fs-readfile-error/);
}));

test({ NODE_DEBUG: 'fs' }, common.mustCall((data) => {
  assert.match(data, /EISDIR/);
  assert.match(data, /test-fs-readfile-error/);
}));

assert.throws(
  () => { fs.readFile(() => {}, common.mustNotCall()); },
  {
    code: 'ERR_INVALID_ARG_TYPE',
    message: 'The "path" argument must be of type string or an instance of ' +
             'Buffer or URL. Received function ',
    name: 'TypeError'
  }
);
