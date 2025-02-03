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
  const filePath = tmpdir.resolve(`close_errors_${++count}.txt`);
  await writeFile(filePath, 'content');
  return filePath;
}

async function checkCloseError(op) {
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
        return originalFd.get.call(this);
      }
    });

    await assert.rejects(op(filePath), {
      name: 'Error',
      message: 'CLOSE_ERROR',
      code: 456,
    });
  } finally {
    Object.defineProperty(FileHandle.prototype, 'fd', originalFd);
  }
}
(async function() {
  tmpdir.refresh();
  await checkCloseError((filePath) => truncate(filePath));
  await checkCloseError((filePath) => readFile(filePath));
  await checkCloseError((filePath) => writeFile(filePath, '123'));
  if (common.isMacOS) {
    await checkCloseError((filePath) => lchmod(filePath, 0o777));
  }
})().then(common.mustCall());
