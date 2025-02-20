'use strict';

require('../common');
const { kMaxLength } = require('buffer');
const assert = require('assert');

const b = Buffer.allocUnsafe(1024);
const c = Buffer.allocUnsafe(512);

let cntr = 0;

{
  // copy 512 bytes, from 0 to 512.
  b.fill(++cntr);
  c.fill(++cntr);
  const copied = b.copy(c, 0, 0, 512);
  assert.strictEqual(copied, 512);
  for (let i = 0; i < c.length; i++) {
    assert.strictEqual(c[i], b[i]);
  }
}

{
  // Current behavior is to coerce values to integers.
  b.fill(++cntr);
  c.fill(++cntr);
  const copied = b.copy(c, '0', '0', '512');
  assert.strictEqual(copied, 512);
  for (let i = 0; i < c.length; i++) {
    assert.strictEqual(c[i], b[i]);
  }
}

{
  // Floats will be converted to integers via `Math.floor`
  b.fill(++cntr);
  c.fill(++cntr);
  const copied = b.copy(c, 0, 0, 512.5);
  assert.strictEqual(copied, 512);
  for (let i = 0; i < c.length; i++) {
    assert.strictEqual(c[i], b[i]);
  }
}

{
  // Copy c into b, without specifying sourceEnd
  b.fill(++cntr);
  c.fill(++cntr);
  const copied = c.copy(b, 0, 0);
  assert.strictEqual(copied, c.length);
  for (let i = 0; i < c.length; i++) {
    assert.strictEqual(b[i], c[i]);
  }
}

{
  // Copy c into b, without specifying sourceStart
  b.fill(++cntr);
  c.fill(++cntr);
  const copied = c.copy(b, 0);
  assert.strictEqual(copied, c.length);
  for (let i = 0; i < c.length; i++) {
    assert.strictEqual(b[i], c[i]);
  }
}

{
  // Copied source range greater than source length
  b.fill(++cntr);
  c.fill(++cntr);
  const copied = c.copy(b, 0, 0, c.length + 1);
  assert.strictEqual(copied, c.length);
  for (let i = 0; i < c.length; i++) {
    assert.strictEqual(b[i], c[i]);
  }
}

{
  // Copy longer buffer b to shorter c without targetStart
  b.fill(++cntr);
  c.fill(++cntr);
  const copied = b.copy(c);
  assert.strictEqual(copied, c.length);
  for (let i = 0; i < c.length; i++) {
    assert.strictEqual(c[i], b[i]);
  }
}

{
  // Copy starting near end of b to c
  b.fill(++cntr);
  c.fill(++cntr);
  const copied = b.copy(c, 0, b.length - Math.floor(c.length / 2));
  assert.strictEqual(copied, Math.floor(c.length / 2));
  for (let i = 0; i < Math.floor(c.length / 2); i++) {
    assert.strictEqual(c[i], b[b.length - Math.floor(c.length / 2) + i]);
  }
  for (let i = Math.floor(c.length / 2) + 1; i < c.length; i++) {
    assert.strictEqual(c[c.length - 1], c[i]);
  }
}

{
  // Try to copy 513 bytes, and check we don't overrun c
  b.fill(++cntr);
  c.fill(++cntr);
  const copied = b.copy(c, 0, 0, 513);
  assert.strictEqual(copied, c.length);
  for (let i = 0; i < c.length; i++) {
    assert.strictEqual(c[i], b[i]);
  }
}

{
  // copy 768 bytes from b into b
  b.fill(++cntr);
  b.fill(++cntr, 256);
  const copied = b.copy(b, 0, 256, 1024);
  assert.strictEqual(copied, 768);
  for (let i = 0; i < b.length; i++) {
    assert.strictEqual(b[i], cntr);
  }
}

// Copy string longer than buffer length (failure will segfault)
const bb = Buffer.allocUnsafe(10);
bb.fill('hello crazy world');


// Try to copy from before the beginning of b. Should not throw.
b.copy(c, 0, 100, 10);

// Throw with invalid source type
assert.throws(
  () => Buffer.prototype.copy.call(0),
  {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError',
  }
);

// Copy throws at negative targetStart
assert.throws(
  () => Buffer.allocUnsafe(5).copy(Buffer.allocUnsafe(5), -1, 0),
  {
    code: 'ERR_OUT_OF_RANGE',
    name: 'RangeError',
    message: 'The value of "targetStart" is out of range. ' +
             'It must be >= 0. Received -1'
  }
);

// Copy throws at negative sourceStart
assert.throws(
  () => Buffer.allocUnsafe(5).copy(Buffer.allocUnsafe(5), 0, -1),
  {
    code: 'ERR_OUT_OF_RANGE',
    name: 'RangeError',
  }
);

// Copy throws if sourceStart is greater than length of source
assert.throws(
  () => Buffer.allocUnsafe(5).copy(Buffer.allocUnsafe(5), 0, 100),
  {
    code: 'ERR_OUT_OF_RANGE',
    name: 'RangeError',
  }
);

{
  // Check sourceEnd resets to targetEnd if former is greater than the latter
  b.fill(++cntr);
  c.fill(++cntr);
  b.copy(c, 0, 0, 1025);
  for (let i = 0; i < c.length; i++) {
    assert.strictEqual(c[i], b[i]);
  }
}

// Throw with negative sourceEnd
assert.throws(
  () => b.copy(c, 0, 0, -1),
  {
    code: 'ERR_OUT_OF_RANGE',
    name: 'RangeError',
    message: 'The value of "sourceEnd" is out of range. ' +
             'It must be >= 0. Received -1'
  }
);

// When sourceStart is greater than sourceEnd, zero copied
assert.strictEqual(b.copy(c, 0, 100, 10), 0);

// When targetStart > targetLength, zero copied
assert.strictEqual(b.copy(c, 512, 0, 10), 0);

// Test that the `target` can be a Uint8Array.
{
  const d = new Uint8Array(c);
  // copy 512 bytes, from 0 to 512.
  b.fill(++cntr);
  d.fill(++cntr);
  const copied = b.copy(d, 0, 0, 512);
  assert.strictEqual(copied, 512);
  for (let i = 0; i < d.length; i++) {
    assert.strictEqual(d[i], b[i]);
  }
}

// Test that the source can be a Uint8Array, too.
{
  const e = new Uint8Array(b);
  // copy 512 bytes, from 0 to 512.
  e.fill(++cntr);
  c.fill(++cntr);
  const copied = Buffer.prototype.copy.call(e, c, 0, 0, 512);
  assert.strictEqual(copied, 512);
  for (let i = 0; i < c.length; i++) {
    assert.strictEqual(c[i], e[i]);
  }
}

// https://github.com/nodejs/node/issues/23668: Do not crash for invalid input.
c.fill('c');
b.copy(c, 'not a valid offset');
// Make sure this acted like a regular copy with `0` offset.
assert.deepStrictEqual(c, b.slice(0, c.length));

{
  c.fill('C');
  assert.throws(() => {
    b.copy(c, { [Symbol.toPrimitive]() { throw new Error('foo'); } });
  }, /foo/);
  // No copying took place:
  assert.deepStrictEqual(c.toString(), 'C'.repeat(c.length));
}

// Ref: https://github.com/nodejs/node/issues/55422#issue-2594486812
if (2 ** 32 + 1 <= kMaxLength) {
  const src = Buffer.alloc(2 ** 32 + 1, 1);
  const dst = Buffer.alloc(2 ** 32 + 1, 2);
  src.copy(dst);
  assert.deepStrictEqual(src, dst);
}
