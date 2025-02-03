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
const assert = require('assert');
const child = require('child_process');
const fixtures = require('../common/fixtures');
const { isMainThread } = require('worker_threads');

if (!isMainThread) {
  common.skip('process.chdir is not available in Workers');
}

if (process.env.TEST_INIT) {
  return process.stdout.write('Loaded successfully!');
}

process.env.TEST_INIT = 1;

function test(file, expected) {
  child.exec(...common.escapePOSIXShell`"${process.execPath}" "${file}"`, common.mustSucceed((out) => {
    assert.strictEqual(out, expected, `'node ${file}' failed!`);
  }));
}

{
  // Change CWD as we do this test so it's not dependent on current CWD
  // being in the test folder
  process.chdir(__dirname);
  test('test-init', 'Loaded successfully!');
  test('test-init.js', 'Loaded successfully!');
}

{
  // test-init-index is in fixtures dir as requested by ry, so go there
  process.chdir(fixtures.path());
  test('test-init-index', 'Loaded successfully!');
}

{
  // Ensures that `node fs` does not mistakenly load the native 'fs' module
  // instead of the desired file and that the fs module loads as
  // expected in node
  process.chdir(fixtures.path('test-init-native'));
  test('fs', 'fs loaded successfully');
}
