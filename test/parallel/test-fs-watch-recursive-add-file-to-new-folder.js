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

// Add a file to newly created folder to already watching folder

const rootDirectory = fs.mkdtempSync(testDir + path.sep);
const testDirectory = path.join(rootDirectory, 'test-3');
fs.mkdirSync(testDirectory);

const filePath = path.join(testDirectory, 'folder-3');

const childrenFile = 'file-4.txt';
const childrenAbsolutePath = path.join(filePath, childrenFile);
const childrenRelativePath = path.join(path.basename(filePath), childrenFile);
let watcherClosed = false;

const watcher = fs.watch(testDirectory, { recursive: true });
watcher.on('change', function(event, filename) {
  if (filename === childrenRelativePath) {
    assert.strictEqual(event, 'rename');
    watcher.close();
    watcherClosed = true;
  }
});

// Do the write with a delay to ensure that the OS is ready to notify us.
setTimeout(() => {
  fs.mkdirSync(filePath);
  fs.writeFileSync(childrenAbsolutePath, 'world');
}, common.platformTimeout(200));

process.once('exit', function() {
  assert(watcherClosed, 'watcher Object was not closed');
});
