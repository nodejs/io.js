'use strict';

const common = require('../common');
const fixtures = require('../common/fixtures');
const fs = require('node:fs');
const fsPromises = require('node:fs/promises');
const read = require('node:util').promisify(fs.read);
const assert = require('node:assert');
const { describe, test } = require('node:test');

describe('fs promises read with optional parameters', async () => {
  const filepath = fixtures.path('x.txt');

  const expected = Buffer.from('xyz\n');
  const defaultBufferAsync = Buffer.alloc(16384);
  const bufferAsOption = Buffer.allocUnsafe(expected.byteLength);

  test('should read the file with default buffer (promisified)', async () => {
    const fd = fs.openSync(filepath, 'r');
    const { bytesRead, buffer } = await read(
      fd,
      common.mustNotMutateObjectDeep({})
    );
    assert.strictEqual(bytesRead, expected.byteLength);
    assert.deepStrictEqual(defaultBufferAsync.byteLength, buffer.byteLength);
  });

  test('should read the file with buffer as option (promisified)', async () => {
    const fd = fs.openSync(filepath, 'r');
    const { bytesRead, buffer } = await read(
      fd,
      bufferAsOption,
      common.mustNotMutateObjectDeep({ position: 0 })
    );
    assert.strictEqual(bytesRead, expected.byteLength);
    assert.deepStrictEqual(bufferAsOption.byteLength, buffer.byteLength);
  });

  test('should read the file with default buffer (fd)', async () => {
    const fd = await fsPromises.open(filepath, 'r');
    const { bytesRead, buffer } = await fd.read(
      common.mustNotMutateObjectDeep({})
    );
    assert.strictEqual(bytesRead, expected.byteLength);
    assert.deepStrictEqual(defaultBufferAsync.byteLength, buffer.byteLength);
  });

  test('should read the file with buffer as option (fd)', async () => {
    const fd = await fsPromises.open(filepath, 'r');
    const { bytesRead, buffer } = await fd.read(
      bufferAsOption,
      common.mustNotMutateObjectDeep({ position: 0 })
    );
    assert.strictEqual(bytesRead, expected.byteLength);
    assert.deepStrictEqual(bufferAsOption.byteLength, buffer.byteLength);
  });
});
