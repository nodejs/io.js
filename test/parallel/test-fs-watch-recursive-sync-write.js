'use strict';

const common = require('../common');
const { watch, writeFileSync } = require('node:fs');
const { join } = require('node:path');
const tmpdir = require('../common/tmpdir.js');
const assert = require('assert');

if (common.isIBMi)
  common.skip('IBMi does not support `fs.watch()`');

// fs-watch on folders have limited capability in AIX.
// The testcase makes use of folder watching, and causes
// hang. This behavior is documented. Skip this for AIX.

if (common.isAIX)
  common.skip('folder watch capability is limited in AIX.');

tmpdir.refresh();

const tmpDir = tmpdir.path;
const filename = join(tmpDir, 'test.file');

const keepalive = setTimeout(() => {
  throw new Error('timed out');
}, common.platformTimeout(30_000));

const watcher = watch(tmpDir, { recursive: true }, common.mustCall((eventType, _filename) => {
  clearTimeout(keepalive);
  watcher.close();
  assert.strictEqual(eventType, 'rename');
  assert.strictEqual(join(tmpDir, _filename), filename);
}));

// Do the write with a delay to ensure that the OS is ready to notify us.
setTimeout(() => {
  writeFileSync(filename, 'foobar2');
}, common.platformTimeout(200));
