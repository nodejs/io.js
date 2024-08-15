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
const debug = require('util').debuglog('test');

const { getTestCases } = require('../common/process-exit-code-cases');
const testCases = getTestCases(false);

if (!process.argv[2]) {
  parent();
} else {
  const i = parseInt(process.argv[2]);
  if (Number.isNaN(i)) {
    debug('Invalid test case index');
    process.exit(100);
    return;
  }
  testCases[i].func();
}

function parent() {
  const { spawn } = require('child_process');
  const node = process.execPath;
  const f = __filename;
  const option = { stdio: [ 0, 1, 'ignore' ] };

  const test = (arg, name = 'child', exit) => {
    spawn(node, [f, arg], option).on('exit', (code) => {
      assert.strictEqual(
        code, exit,
        `wrong exit for ${arg}-${name}\nexpected:${exit} but got:${code}`);
      debug(`ok - ${arg} exited with ${exit}`);
    });
  };

  testCases.forEach((tc, i) => test(i, tc.func.name, tc.result));
}
