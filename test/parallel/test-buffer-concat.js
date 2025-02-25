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
const { kMaxLength } = require('buffer');

const zero = [];
const one = [ Buffer.from('asdf') ];
const long = [];
for (let i = 0; i < 10; i++) long.push(Buffer.from('asdf'));

const flatZero = Buffer.concat(zero);
const flatOne = Buffer.concat(one);
const flatLong = Buffer.concat(long);
const flatLongLen = Buffer.concat(long, 40);

assert.strictEqual(flatZero.length, 0);
assert.strictEqual(flatOne.toString(), 'asdf');

const check = 'asdf'.repeat(10);

// A special case where concat used to return the first item,
// if the length is one. This check is to make sure that we don't do that.
assert.notStrictEqual(flatOne, one[0]);
assert.strictEqual(flatLong.toString(), check);
assert.strictEqual(flatLongLen.toString(), check);

[undefined, null, Buffer.from('hello')].forEach((value) => {
  assert.throws(() => {
    Buffer.concat(value);
  }, {
    code: 'ERR_INVALID_ARG_TYPE',
    message: 'The "list" argument must be an instance of Array.' +
             `${common.invalidArgTypeHelper(value)}`
  });
});

[[42], ['hello', Buffer.from('world')]].forEach((value) => {
  assert.throws(() => {
    Buffer.concat(value);
  }, {
    code: 'ERR_INVALID_ARG_TYPE',
    message: 'The "list[0]" argument must be an instance of Buffer ' +
             `or Uint8Array.${common.invalidArgTypeHelper(value[0])}`
  });
});

assert.throws(() => {
  Buffer.concat([Buffer.from('hello'), 3]);
}, {
  code: 'ERR_INVALID_ARG_TYPE',
  message: 'The "list[1]" argument must be an instance of Buffer ' +
           'or Uint8Array. Received type number (3)'
});

// eslint-disable-next-line node-core/crypto-check
const random10 = common.hasCrypto ?
  require('crypto').randomBytes(10) :
  Buffer.alloc(10, 1);
const empty = Buffer.alloc(0);

assert.notDeepStrictEqual(random10, empty);
assert.notDeepStrictEqual(random10, Buffer.alloc(10));

assert.deepStrictEqual(Buffer.concat([], 100), empty);
assert.deepStrictEqual(Buffer.concat([random10], 0), empty);
assert.deepStrictEqual(Buffer.concat([random10], 10), random10);
assert.deepStrictEqual(Buffer.concat([random10, random10], 10), random10);
assert.deepStrictEqual(Buffer.concat([empty, random10]), random10);
assert.deepStrictEqual(Buffer.concat([random10, empty, empty]), random10);

// The tail should be zero-filled
assert.deepStrictEqual(Buffer.concat([empty], 100), Buffer.alloc(100));
assert.deepStrictEqual(Buffer.concat([empty], 4096), Buffer.alloc(4096));
assert.deepStrictEqual(
  Buffer.concat([random10], 40),
  Buffer.concat([random10, Buffer.alloc(30)]));

assert.deepStrictEqual(Buffer.concat([new Uint8Array([0x41, 0x42]),
                                      new Uint8Array([0x43, 0x44])]),
                       Buffer.from('ABCD'));

// Ref: https://github.com/nodejs/node/issues/55422#issue-2594486812
if (2 ** 32 + 1 <= kMaxLength) {
  const a = Buffer.alloc(2 ** 31, 0);
  const b = Buffer.alloc(2 ** 31, 1);
  const c = Buffer.alloc(1, 2);
  const destin = Buffer.concat([a, b, c]);

  assert.deepStrictEqual(destin.subarray(0, 2 ** 31), a);
  assert.deepStrictEqual(destin.subarray(2 ** 31, 2 ** 32), b);
  assert.deepStrictEqual(destin.subarray(2 ** 32), c);
}
