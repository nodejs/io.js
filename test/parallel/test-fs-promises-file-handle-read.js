'use strict';

const common = require('../common');

// The following tests validate base functionality for the fs.promises
// FileHandle.read method.

const fs = require('fs');
const { open } = fs.promises;
const path = require('path');
const fixtures = require('../common/fixtures');
const tmpdir = require('../common/tmpdir');
const assert = require('assert');
const tmpDir = tmpdir.path;

async function read(fileHandle, buffer, offset, length, position, options) {
  return options?.useConf ?
    fileHandle.read({ buffer, offset, length, position }) :
    fileHandle.read(buffer, offset, length, position);
}

async function validateRead(data, file, options) {
  const filePath = path.resolve(tmpDir, file);
  const buffer = Buffer.from(data, 'utf8');

  const fd = fs.openSync(filePath, 'w+');
  const fileHandle = await open(filePath, 'w+');
  const streamFileHandle = await open(filePath, 'w+');

  fs.writeSync(fd, buffer, 0, buffer.length);
  fs.closeSync(fd);

  fileHandle.on('close', common.mustCall());
  const readAsyncHandle =
    await read(fileHandle, Buffer.alloc(11), 0, 11, 0, options);
  assert.deepStrictEqual(data.length, readAsyncHandle.bytesRead);
  if (data.length)
    assert.deepStrictEqual(buffer, readAsyncHandle.buffer);
  await fileHandle.close();

  const stream = fs.createReadStream(null, { fd: streamFileHandle });
  let streamData = Buffer.alloc(0);
  for await (const chunk of stream)
    streamData = Buffer.from(chunk);
  assert.deepStrictEqual(buffer, streamData);
  if (data.length)
    assert.deepStrictEqual(streamData, readAsyncHandle.buffer);
  await streamFileHandle.close();
}

async function validateLargeRead(options) {
  // Reading beyond file length (3 in this case) should return no data.
  // This is a test for a bug where reads > uint32 would return data
  // from the current position in the file.
  const filePath = fixtures.path('x.txt');
  const fileHandle = await open(filePath, 'r');
  const pos = 0xffffffff + 1; // max-uint32 + 1
  const readHandle =
    await read(fileHandle, Buffer.alloc(1), 0, 1, pos, options);

  assert.strictEqual(readHandle.bytesRead, 0);
}

async function validateReadNoParams() {
  const filePath = fixtures.path('x.txt');
  const fileHandle = await open(filePath, 'r');
  // Should not throw
  await fileHandle.read();
}

// Validates that the zero position is respected after the position has been
// moved. The test iterates over the xyz chars twice making sure that the values
// are read from the correct position.
async function validateReadWithPositionZero() {
  const opts = { useConf: true };
  const filePath = fixtures.path('x.txt');
  const fileHandle = await open(filePath, 'r');
  const expectedSequence = ['x', 'y', 'z'];

  for (let i = 0; i < expectedSequence.length * 2; i++) {
    const len = 1;
    const pos = i % 3;
    const buf = Buffer.alloc(len);
    const { bytesRead } = await read(fileHandle, buf, 0, len, pos, opts);
    assert.strictEqual(bytesRead, len);
    assert.strictEqual(buf.toString(), expectedSequence[pos]);
  }
}

async function validateReadLength(len) {
  const buf = Buffer.alloc(4);
  const opts = { useConf: true };
  const filePath = fixtures.path('x.txt');
  const fileHandle = await open(filePath, 'r');
  const { bytesRead } = await read(fileHandle, buf, 0, len, 0, opts);
  assert.strictEqual(bytesRead, len);
}

async function validateReadWithNoOptions(byte) {
  const buf = Buffer.alloc(byte);
  const filePath = fixtures.path('x.txt');
  const fileHandle = await open(filePath, 'r');
  let response = await fileHandle.read(buf);
  assert.strictEqual(response.bytesRead, byte);
  response = await read(fileHandle, buf, 0, undefined, 0);
  assert.strictEqual(response.bytesRead, byte);
  response = await read(fileHandle, buf, 0, null, 0);
  assert.strictEqual(response.bytesRead, byte);
  response = await read(fileHandle, buf, 0, undefined, 0, { useConf: true });
  assert.strictEqual(response.bytesRead, byte);
  response = await read(fileHandle, buf, 0, null, 0, { useConf: true });
  assert.strictEqual(response.bytesRead, byte);
}

(async function() {
  tmpdir.refresh();
  await validateRead('Hello world', 'read-file', { useConf: false });
  await validateRead('', 'read-empty-file', { useConf: false });
  await validateRead('Hello world', 'read-file-conf', { useConf: true });
  await validateRead('', 'read-empty-file-conf', { useConf: true });
  await validateLargeRead({ useConf: false });
  await validateLargeRead({ useConf: true });
  await validateReadNoParams();
  await validateReadWithPositionZero();
  await validateReadLength(0);
  await validateReadLength(1);
  await validateReadWithNoOptions(0);
  await validateReadWithNoOptions(1);
})().then(common.mustCall());
