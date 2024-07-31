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

const tmpdir = require('../common/tmpdir');
tmpdir.refresh();

const filename = tmpdir.resolve('test.txt');
const fixtures = require('../common/fixtures');
const s = fixtures.utf8TestText;

fs.writeFile(filename, s, common.mustSucceed(() => {
  fs.readFile(filename, common.mustSucceed((buffer) => {
    assert.strictEqual(Buffer.byteLength(s), buffer.length);
  }));
}));

// Test that writeFile accepts buffers.
const filename2 = tmpdir.resolve('test2.txt');
const buf = Buffer.from(s, 'utf8');

fs.writeFile(filename2, buf, common.mustSucceed(() => {
  fs.readFile(filename2, common.mustSucceed((buffer) => {
    assert.strictEqual(buf.length, buffer.length);
  }));
}));

// Test that writeFile accepts file descriptors.
const filename4 = tmpdir.resolve('test4.txt');

fs.open(filename4, 'w+', common.mustSucceed((fd) => {
  fs.writeFile(fd, s, common.mustSucceed(() => {
    fs.close(fd, common.mustSucceed(() => {
      fs.readFile(filename4, common.mustSucceed((buffer) => {
        assert.strictEqual(Buffer.byteLength(s), buffer.length);
      }));
    }));
  }));
}));


{
  // Test that writeFile is cancellable with an AbortSignal.
  // Before the operation has started
  const controller = new AbortController();
  const signal = controller.signal;
  const filename3 = tmpdir.resolve('test3.txt');

  fs.writeFile(filename3, s, { signal }, common.mustCall((err) => {
    assert.strictEqual(err.name, 'AbortError');
  }));

  controller.abort();
}

{
  // Test that writeFile is cancellable with an AbortSignal.
  // After the operation has started
  const controller = new AbortController();
  const signal = controller.signal;
  const filename4 = tmpdir.resolve('test5.txt');

  fs.writeFile(filename4, s, { signal }, common.mustCall((err) => {
    assert.strictEqual(err.name, 'AbortError');
  }));

  process.nextTick(() => controller.abort());
}

{
  // Test read-only mode
  const filename = tmpdir.resolve('test6.txt');
  fs.writeFileSync(filename, '');
  fs.writeFile(filename, s, { flag: 'r' }, common.expectsError(/EBADF/));
}
