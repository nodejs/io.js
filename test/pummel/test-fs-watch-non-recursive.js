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

if (common.isIBMi) {
  common.skip('IBMi does not support fs.watch()');
}

const path = require('path');
const fs = require('fs');
const assert = require('assert');
const tmpdir = require('../common/tmpdir');
tmpdir.refresh();

const testDir = tmpdir.path;
const testsubdir = path.join(testDir, 'testsubdir');
const filepath = path.join(testsubdir, 'watch.txt');

fs.mkdirSync(testsubdir, 0o700);

function doWatch() {
  const watcher = fs.watch(testDir, { persistent: true }, (event, filename) => {
    // This function may be called with the directory depending on timing but
    // must not be called with the file..
    assert.strictEqual(filename, 'testsubdir');
  });
  setTimeout(() => {
    fs.writeFileSync(filepath, 'test');
  }, 100);
  setTimeout(() => {
    watcher.close();
  }, 500);
}

if (common.isMacOS) {
  // On macOS delay watcher start to avoid leaking previous events.
  // Refs: https://github.com/libuv/libuv/pull/4503
  setTimeout(doWatch, common.platformTimeout(100));
} else {
  doWatch();
}
