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

const zlib = require('node:zlib');
const assert = require('node:assert');
const { test } = require('node:test');

test('zlib should not allow writing after close', async (t) => {
  const { promise, resolve } = Promise.withResolvers();
  const closeCallback = t.mock.fn();
  zlib.gzip('hello', function() {
    const unzip = zlib.createGunzip();
    unzip.close(closeCallback);
    unzip.write('asd', function(err) {
      assert.strictEqual(err.code, 'ERR_STREAM_DESTROYED');
      assert.strictEqual(err.name, 'Error');
      assert.strictEqual(err.message, 'Cannot call write after a stream was destroyed');
      resolve();
    });
  });
  await promise;
  assert.strictEqual(closeCallback.mock.callCount(), 1);
});
