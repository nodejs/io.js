'use strict';
// Flags: --expose-internals

const common = require('../common');
const tmpdir = require('../common/tmpdir');

// The following tests validate aggregate errors are thrown correctly
// when both an operation and close throw.

const {
  readFile,
  writeFile,
  truncate,
  lchmod,
} = require('fs/promises');
const {
  FileHandle,
} = require('internal/fs/promises');

const assert = require('assert');
const originalFd = Object.getOwnPropertyDescriptor(FileHandle.prototype, 'fd');

let count = 0;
async function createFile() {
  const filePath = tmpdir.resolve(`aggregate_errors_${++count}.txt`);
  await writeFile(filePath, 'content');
  return filePath;
}

async function checkAggregateError(op) {
  try {
    const filePath = await createFile();
    Object.defineProperty(FileHandle.prototype, 'fd', {
      get: function() {
        // Close is set by using a setter,
        // so it needs to be set on the instance.
        const originalClose = this.close;
        this.close = async () => {
          // close the file
          await originalClose.call(this);
          const closeError = new Error('CLOSE_ERROR');
          closeError.code = 456;
          throw closeError;
        };
        const opError = new Error('INTERNAL_ERROR');
        opError.code = 123;
        throw opError;
      }
    });

    await assert.rejects(op(filePath), common.mustCall((err) => {
      assert.strictEqual(err.name, 'AggregateError');
      assert.strictEqual(err.code, 123);
      assert.strictEqual(err.errors.length, 2);
      assert.strictEqual(err.errors[0].message, 'INTERNAL_ERROR');
      assert.strictEqual(err.errors[1].message, 'CLOSE_ERROR');
      return true;
    }));
  } finally {
    Object.defineProperty(FileHandle.prototype, 'fd', originalFd);
  }
}
(async function() {
  tmpdir.refresh();
  await checkAggregateError((filePath) => truncate(filePath));
  await checkAggregateError((filePath) => readFile(filePath));
  await checkAggregateError((filePath) => writeFile(filePath, '123'));
  if (common.isMacOS) {
    await checkAggregateError((filePath) => lchmod(filePath, 0o777));
  }
})().then(common.mustCall());
