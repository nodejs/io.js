'use strict';

require('../common');

// The following tests validate base functionality for the fs.promises
// FileHandle.appendFile method.

const fs = require('fs');
const { open } = fs.promises;
const path = require('path');
const tmpdir = require('../common/tmpdir');
const assert = require('assert');
const tmpDir = tmpdir.path;

tmpdir.refresh();

async function validateAppendBuffer() {
  const filePath = path.resolve(tmpDir, 'tmp-append-file-buffer.txt');
  const fileHandle = await open(filePath, 'a');
  const buffer = Buffer.from('a&Dp'.repeat(100), 'utf8');

  await fileHandle.appendFile(buffer);
  const appendedFileData = fs.readFileSync(filePath);
  assert.deepStrictEqual(appendedFileData, buffer);

  await fileHandle.close();
}

async function validateAppendString() {
  const filePath = path.resolve(tmpDir, 'tmp-append-file-string.txt');
  const fileHandle = await open(filePath, 'a');
  const string = 'x~yz'.repeat(100);

  await fileHandle.appendFile(string);
  const stringAsBuffer = Buffer.from(string, 'utf8');
  const appendedFileData = fs.readFileSync(filePath);
  assert.deepStrictEqual(appendedFileData, stringAsBuffer);

  await fileHandle.close();
}

async function doAppendAndCancel() {
  const filePathForHandle = path.resolve(tmpDir, 'dogs-running.txt');
  const fileHandle = await open(filePathForHandle, 'w+');
  const buffer = Buffer.from('dogs running'.repeat(512 * 1024), 'utf8');
  const controller = new AbortController();
  const { signal } = controller;
  process.nextTick(() => controller.abort());
  await assert.rejects(fileHandle.appendFile(buffer, { signal }), {
    name: 'AbortError'
  });
  await fileHandle.close();
}

Promise.all([
  validateAppendBuffer(),
  validateAppendString(),
  doAppendAndCancel(),
]);
