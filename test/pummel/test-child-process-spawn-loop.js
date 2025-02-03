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

const spawn = require('child_process').spawn;
const python = process.env.PYTHON || (common.isWindows ? 'python' : 'python3');

const SIZE = 1000 * 1024;
const N = 40;
let finished = false;

function doSpawn(i) {
  const child = spawn(python, ['-c', `print(${SIZE} * "C")`]);
  let count = 0;

  child.stdout.setEncoding('ascii');
  child.stdout.on('data', (chunk) => {
    count += chunk.length;
  });

  child.stderr.on('data', (chunk) => {
    console.log(`stderr: ${chunk}`);
  });

  child.on('close', () => {
    // + 1 for \n or + 2 for \r\n on Windows
    assert.strictEqual(count, SIZE + (common.isWindows ? 2 : 1));
    if (i < N) {
      doSpawn(i + 1);
    } else {
      finished = true;
    }
  });
}

doSpawn(0);

process.on('exit', () => {
  assert.ok(finished);
});
