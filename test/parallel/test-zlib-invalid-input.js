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
// Test uncompressing invalid input

require('../common');

const assert = require('node:assert');
const zlib = require('node:zlib');
const { test } = require('node:test');

test('uncompressing invalid input', async (t) => {
  const nonStringInputs = [
    1,
    true,
    { a: 1 },
    ['a'],
  ];

  // zlib.Unzip classes need to get valid data, or else they'll throw.
  const unzips = [
    new zlib.Unzip(),
    new zlib.Gunzip(),
    new zlib.Inflate(),
    new zlib.InflateRaw(),
    new zlib.BrotliDecompress(),
  ];

  for (const input of nonStringInputs) {
    assert.throws(() => {
      zlib.gunzip(input);
    }, {
      name: 'TypeError',
      code: 'ERR_INVALID_ARG_TYPE'
    });
  }

  for (const uz of unzips) {
    const { promise, resolve, reject } = Promise.withResolvers();
    uz.on('error', resolve);
    uz.on('end', reject);

    // This will trigger error event
    uz.write('this is not valid compressed data.');
    await promise;
  }
});

// This test ensures that the BrotliCompress function throws
// ERR_INVALID_ARG_TYPE when the values of the `params` key-value object are
// neither numbers nor booleans.
test('ensure BrotliCompress throws error on invalid params', async (t) => {
  assert.throws(() => new zlib.BrotliCompress({
    params: {
      [zlib.constants.BROTLI_PARAM_MODE]: 'lol'
    }
  }), {
    code: 'ERR_INVALID_ARG_TYPE'
  });
});

test('validate failed init', async (t) => {
  assert.throws(
    () => zlib.createGzip({ chunkSize: 0 }),
    {
      code: 'ERR_OUT_OF_RANGE',
      name: 'RangeError',
      message: 'The value of "options.chunkSize" is out of range. It must ' +
               'be >= 64. Received 0'
    }
  );

  assert.throws(
    () => zlib.createGzip({ windowBits: 0 }),
    {
      code: 'ERR_OUT_OF_RANGE',
      name: 'RangeError',
      message: 'The value of "options.windowBits" is out of range. It must ' +
               'be >= 9 and <= 15. Received 0'
    }
  );

  assert.throws(
    () => zlib.createGzip({ memLevel: 0 }),
    {
      code: 'ERR_OUT_OF_RANGE',
      name: 'RangeError',
      message: 'The value of "options.memLevel" is out of range. It must ' +
               'be >= 1 and <= 9. Received 0'
    }
  );

  {
    const stream = zlib.createGzip({ level: NaN });
    assert.strictEqual(stream._level, zlib.constants.Z_DEFAULT_COMPRESSION);
  }

  {
    const stream = zlib.createGzip({ strategy: NaN });
    assert.strictEqual(stream._strategy, zlib.constants.Z_DEFAULT_STRATEGY);
  }
});
