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
if (!common.isWindows)
  common.skip('this test is Windows-specific.');

const fs = require('fs');
const path = require('path');

const tmpdir = require('../common/tmpdir');

// Make a path that will be at least 260 chars long.
const fileNameLen = Math.max(260 - tmpdir.path.length - 1, 1);
const fileName = tmpdir.resolve('x'.repeat(fileNameLen));
const fullPath = path.resolve(fileName);

tmpdir.refresh();

console.log({
  filenameLength: fileName.length,
  fullPathLength: fullPath.length
});

fs.writeFile(fullPath, 'ok', common.mustSucceed(() => {
  fs.stat(fullPath, common.mustSucceed());

  // Tests https://github.com/nodejs/node/issues/39721
  fs.realpath.native(fullPath, common.mustSucceed());

  // Tests https://github.com/nodejs/node/issues/51031
  fs.promises.realpath(fullPath).then(common.mustCall(), common.mustNotCall());
}));
