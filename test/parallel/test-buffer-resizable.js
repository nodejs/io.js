// Flags: --no-warnings
'use strict';

require('../common');
const { Buffer } = require('node:buffer');
const { strictEqual } = require('node:assert');
const { describe, it } = require('node:test');

describe('Using resizable ArrayBuffer with Buffer...', () => {
  it('works as expected', () => {
    const ab = new ArrayBuffer(10, { maxByteLength: 20 });
    const buffer = Buffer.from(ab, 1);
    strictEqual(buffer.byteLength, 9);
    ab.resize(15);
    strictEqual(buffer.byteLength, 14);
    ab.resize(5);
    strictEqual(buffer.byteLength, 4);
  });

  it('works with the deprecated constructor also', () => {
    const ab = new ArrayBuffer(10, { maxByteLength: 20 });
    const buffer = new Buffer(ab, 1);
    strictEqual(buffer.byteLength, 9);
    ab.resize(15);
    strictEqual(buffer.byteLength, 14);
    ab.resize(5);
    strictEqual(buffer.byteLength, 4);
  });
});
