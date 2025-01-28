'use strict';
const common = require('../common');

// This test ensures that fs.readFile correctly returns the
// contents of varying-sized files.

const tmpdir = require('../../test/common/tmpdir');
const assert = require('assert');
const path = require('node:path');
const fs = require('fs');

const prefix = `.removeme-fs-readfile-${process.pid}`;

tmpdir.refresh();

const fileInfo = [
  { name: tmpdir.resolve(`${prefix}-1K.txt`),
    len: 1024 },
  { name: tmpdir.resolve(`${prefix}-64K.txt`),
    len: 64 * 1024 },
  { name: tmpdir.resolve(`${prefix}-64KLessOne.txt`),
    len: (64 * 1024) - 1 },
  { name: tmpdir.resolve(`${prefix}-1M.txt`),
    len: 1 * 1024 * 1024 },
  { name: tmpdir.resolve(`${prefix}-1MPlusOne.txt`),
    len: (1 * 1024 * 1024) + 1 },
];

// Populate each fileInfo (and file) with unique fill.
const sectorSize = 512;
for (const e of fileInfo) {
  e.contents = Buffer.allocUnsafe(e.len);

  // This accounts for anything unusual in Node's implementation of readFile.
  // Using e.g. 'aa...aa' would miss bugs like Node re-reading
  // the same section twice instead of two separate sections.
  for (let offset = 0; offset < e.len; offset += sectorSize) {
    const fillByte = 256 * Math.random();
    const nBytesToFill = Math.min(sectorSize, e.len - offset);
    e.contents.fill(fillByte, offset, offset + nBytesToFill);
  }

  fs.writeFileSync(e.name, e.contents);
}
// All files are now populated.

// Test readFile on each size.
for (const e of fileInfo) {
  fs.readFile(e.name, common.mustCall((err, buf) => {
    console.log(`Validating readFile on file ${e.name} of length ${e.len}`);
    assert.ifError(err);
    assert.deepStrictEqual(buf, e.contents);
  }));
}

// Test to verify that readFile() and readFileSync() can handle large files
{
  const kLargeFileSize = 3 * 1024 * 1024 * 1024; // 3 GiB

  const file = path.join(tmpdir.path, 'temp-large-file.txt');
  fs.writeFileSync(file, Buffer.alloc(1024));
  fs.truncateSync(file, kLargeFileSize);

  fs.readFile(file, (err, data) => {
    if (err) {
      console.error('Error reading file:', err);
    } else {
      console.log('File read successfully:', data.length);
    }
  });
}

{
  // Test cancellation, before
  const signal = AbortSignal.abort();
  fs.readFile(fileInfo[0].name, { signal }, common.mustCall((err, buf) => {
    assert.strictEqual(err.name, 'AbortError');
  }));
}
{
  // Test cancellation, during read
  const controller = new AbortController();
  const signal = controller.signal;
  fs.readFile(fileInfo[0].name, { signal }, common.mustCall((err, buf) => {
    assert.strictEqual(err.name, 'AbortError');
  }));
  process.nextTick(() => controller.abort());
}
{
  // Verify that if something different than Abortcontroller.signal
  // is passed, ERR_INVALID_ARG_TYPE is thrown
  assert.throws(() => {
    const callback = common.mustNotCall();
    fs.readFile(fileInfo[0].name, { signal: 'hello' }, callback);
  }, { code: 'ERR_INVALID_ARG_TYPE', name: 'TypeError' });
}
