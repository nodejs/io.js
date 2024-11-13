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

const assert = require('node:assert');
const zlib = require('node:zlib');
const { test } = require('node:test');

test('zlib should accept writing after flush', async () => {
  for (const [createCompress, createDecompress] of [
    [zlib.createGzip, zlib.createGunzip],
    [zlib.createBrotliCompress, zlib.createBrotliDecompress],
  ]) {
    const { promise, resolve, reject } = Promise.withResolvers();
    const gzip = createCompress();
    const gunz = createDecompress();

    gzip.pipe(gunz);

    let output = '';
    const input = 'A line of data\n';
    gunz.setEncoding('utf8');
    gunz.on('error', reject);
    gunz.on('data', (c) => output += c);
    gunz.on('end', () => {
      assert.strictEqual(output, input);
      resolve();
    });

    // Make sure that flush/write doesn't trigger an assert failure
    gzip.flush();
    gzip.write(input);
    gzip.end();
    gunz.read(0);
    await promise;
  }
});
