'use strict';

const common = require('../common');

// The following tests validate base functionality for the fs.promises
// FileHandle.readFile method.

const fs = require('node:fs');
const {
  open,
  readFile,
  truncate,
  writeFile,
} = fs.promises;
const path = require('node:path');
const os = require('node:os');
const tmpdir = require('../common/tmpdir');
const tick = require('../common/tick');
const assert = require('node:assert');
const tmpDir = tmpdir.path;

tmpdir.refresh();

async function validateReadFile() {
  const filePath = path.resolve(tmpDir, 'tmp-read-file.txt');
  const fileHandle = await open(filePath, 'w+');
  const buffer = Buffer.from('Hello world'.repeat(100), 'utf8');

  const fd = fs.openSync(filePath, 'w+');
  fs.writeSync(fd, buffer, 0, buffer.length);
  fs.closeSync(fd);

  const readFileData = await fileHandle.readFile();
  assert.deepStrictEqual(buffer, readFileData);

  await fileHandle.close();
}

async function validateLargeFileSupport() {
  const LARGE_FILE_SIZE = 3 * 1024 * 1024 * 1024; // 3 GiB
  const FILE_PATH = path.join(os.tmpdir(), 'large-virtual-file.bin');

  function createVirtualLargeFile() {
    return Buffer.alloc(LARGE_FILE_SIZE, 'A');
  }

  const virtualFile = createVirtualLargeFile();

  await writeFile(FILE_PATH, virtualFile);

  const buffer = await readFile(FILE_PATH);

  assert.strictEqual(
    buffer.length,
    LARGE_FILE_SIZE,
    `Expected file size to be ${LARGE_FILE_SIZE}, but got ${buffer.length}`
  );

  await fs.promises.unlink(FILE_PATH);
}

async function validateReadFileProc() {
  // Test to make sure reading a file under the /proc directory works. Adapted
  // from test-fs-read-file-sync-hostname.js.
  // Refs:
  // - https://groups.google.com/forum/#!topic/nodejs-dev/rxZ_RoH1Gn0
  // - https://github.com/nodejs/node/issues/21331

  // Test is Linux-specific.
  if (!common.isLinux)
    return;

  const fileHandle = await open('/proc/sys/kernel/hostname', 'r');
  const hostname = await fileHandle.readFile();
  assert.ok(hostname.length > 0);
}

async function doReadAndCancel() {
  // Signal aborted from the start
  {
    const filePathForHandle = path.resolve(tmpDir, 'dogs-running.txt');
    const fileHandle = await open(filePathForHandle, 'w+');
    try {
      const buffer = Buffer.from('Dogs running'.repeat(10000), 'utf8');
      fs.writeFileSync(filePathForHandle, buffer);
      const signal = AbortSignal.abort();
      await assert.rejects(readFile(fileHandle, common.mustNotMutateObjectDeep({ signal })), {
        name: 'AbortError'
      });
    } finally {
      await fileHandle.close();
    }
  }

  // Signal aborted on first tick
  {
    const filePathForHandle = path.resolve(tmpDir, 'dogs-running1.txt');
    const fileHandle = await open(filePathForHandle, 'w+');
    const buffer = Buffer.from('Dogs running'.repeat(10000), 'utf8');
    fs.writeFileSync(filePathForHandle, buffer);
    const controller = new AbortController();
    const { signal } = controller;
    process.nextTick(() => controller.abort());
    await assert.rejects(readFile(fileHandle, common.mustNotMutateObjectDeep({ signal })), {
      name: 'AbortError'
    }, 'tick-0');
    await fileHandle.close();
  }

  // Signal aborted right before buffer read
  {
    const newFile = path.resolve(tmpDir, 'dogs-running2.txt');
    const buffer = Buffer.from('Dogs running'.repeat(1000), 'utf8');
    fs.writeFileSync(newFile, buffer);

    const fileHandle = await open(newFile, 'r');

    const controller = new AbortController();
    const { signal } = controller;
    tick(1, () => controller.abort());
    await assert.rejects(fileHandle.readFile(common.mustNotMutateObjectDeep({ signal, encoding: 'utf8' })), {
      name: 'AbortError'
    }, 'tick-1');

    await fileHandle.close();
  }

  // For validates the ability of the filesystem module to handle large files
  {
    const largeFileSize = 5 * 1024 * 1024 * 1024; // 5 GiB
    const newFile = path.resolve(tmpDir, 'dogs-running3.txt');

    if (!tmpdir.hasEnoughSpace(largeFileSize)) {
      common.printSkipMessage(`Not enough space in ${tmpDir}`);
    } else {
      await writeFile(newFile, Buffer.from('0'));
      await truncate(newFile, largeFileSize);

      const fileHandle = await open(newFile, 'r');

      const data = await fileHandle.readFile();
      console.log(`File read successfully, size: ${data.length} bytes`);
      await fileHandle.close();
    }
  }
}

validateReadFile()
  .then(validateLargeFileSupport)
  .then(validateReadFileProc)
  .then(doReadAndCancel)
  .then(common.mustCall());
