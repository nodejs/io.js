'use strict';

const common = require('../common');

if (common.isIBMi)
  common.skip('IBMi does not support `fs.watch()`');

// fs-watch on folders have limited capability in AIX.
// The testcase makes use of folder watching, and causes
// hang. This behavior is documented. Skip this for AIX.

if (common.isAIX)
  common.skip('folder watch capability is limited in AIX.');

const assert = require('assert');
const path = require('path');
const fs = require('fs');

const tmpdir = require('../common/tmpdir');
const testDir = tmpdir.path;
tmpdir.refresh();

// Watch a folder and update an already existing file in it.

const rootDirectory = fs.mkdtempSync(testDir + path.sep);
const testDirectory = path.join(rootDirectory, 'test-0');
fs.mkdirSync(testDirectory);

const testFile = path.join(testDirectory, 'file-1.txt');
fs.writeFileSync(testFile, 'hello');

const watcher = fs.watch(testDirectory, { recursive: true });
watcher.on('change', common.mustCallAtLeast(function(event, filename) {
  // Libuv inconsistenly emits a rename event for the file we are watching
  assert.ok(event === 'change' || event === 'rename');

  if (filename === path.basename(testFile)) {
    watcher.close();
  }
}));

// Do the write with a delay to ensure that the OS is ready to notify us.
setTimeout(() => {
  fs.writeFileSync(testFile, 'hello');
}, common.platformTimeout(200));
