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
const common = require('../../common');
const assert = require('assert');
const repl = require('repl');
const stream = require('stream');
const path = require('path');
let buildPath = path.join(__dirname, 'build', common.buildType, 'binding');
// On Windows, escape backslashes in the path before passing it to REPL.
if (common.isWindows)
  buildPath = buildPath.replace(/\\/g, '/');
let cb_ran = false;

process.on('exit', () => {
  assert(cb_ran);
  console.log('ok');
});

const lines = [
  // This line shouldn't cause an assertion error.
  `require(${JSON.stringify(buildPath)})` +
  // Log output to double check callback ran.
  '.method(function(v1, v2) {' +
  'console.log(\'cb_ran\'); return v1 === true && v2 === false; });',
];

const dInput = new stream.Readable();
const dOutput = new stream.Writable();

dInput._read = function _read() {
  while (lines.length > 0 && this.push(lines.shift()));
  if (lines.length === 0)
    this.push(null);
};

dOutput._write = function _write(chunk, encoding, cb) {
  if (chunk.toString().startsWith('cb_ran'))
    cb_ran = true;
  cb();
};

const options = {
  input: dInput,
  output: dOutput,
  terminal: false,
  ignoreUndefined: true,
};

// Run commands from fake REPL.
repl.start(options);
