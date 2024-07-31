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
const fs = require('fs');

const currentFileData = 'ABCD';
const m = 0o600;
const num = 220;
const tmpdir = require('../common/tmpdir');
const fixtures = require('../common/fixtures');
const data = fixtures.utf8TestText;

tmpdir.refresh();

// Test that empty file will be created and have content added.
const filename = tmpdir.resolve('append-sync.txt');

fs.appendFileSync(filename, data);

const fileData = fs.readFileSync(filename);

assert.strictEqual(Buffer.byteLength(data), fileData.length);

// Test that appends data to a non empty file.
const filename2 = tmpdir.resolve('append-sync2.txt');
fs.writeFileSync(filename2, currentFileData);

fs.appendFileSync(filename2, data);

const fileData2 = fs.readFileSync(filename2);

assert.strictEqual(Buffer.byteLength(data) + currentFileData.length,
                   fileData2.length);

// Test that appendFileSync accepts buffers.
const filename3 = tmpdir.resolve('append-sync3.txt');
fs.writeFileSync(filename3, currentFileData);

const buf = Buffer.from(data, 'utf8');
fs.appendFileSync(filename3, buf);

const fileData3 = fs.readFileSync(filename3);

assert.strictEqual(buf.length + currentFileData.length, fileData3.length);

const filename4 = tmpdir.resolve('append-sync4.txt');
fs.writeFileSync(filename4, currentFileData, common.mustNotMutateObjectDeep({ mode: m }));

[
  true, false, 0, 1, Infinity, () => {}, {}, [], undefined, null,
].forEach((value) => {
  assert.throws(
    () => fs.appendFileSync(filename4, value, common.mustNotMutateObjectDeep({ mode: m })),
    { message: /data/, code: 'ERR_INVALID_ARG_TYPE' }
  );
});
fs.appendFileSync(filename4, `${num}`, common.mustNotMutateObjectDeep({ mode: m }));

// Windows permissions aren't Unix.
if (!common.isWindows) {
  const st = fs.statSync(filename4);
  assert.strictEqual(st.mode & 0o700, m);
}

const fileData4 = fs.readFileSync(filename4);

assert.strictEqual(Buffer.byteLength(String(num)) + currentFileData.length,
                   fileData4.length);

// Test that appendFile accepts file descriptors.
const filename5 = tmpdir.resolve('append-sync5.txt');
fs.writeFileSync(filename5, currentFileData);

const filename5fd = fs.openSync(filename5, 'a+', 0o600);
fs.appendFileSync(filename5fd, data);
fs.closeSync(filename5fd);

const fileData5 = fs.readFileSync(filename5);

assert.strictEqual(Buffer.byteLength(data) + currentFileData.length,
                   fileData5.length);
