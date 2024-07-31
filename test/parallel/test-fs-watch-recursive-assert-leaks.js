'use strict';

const common = require('../common');
const { setTimeout } = require('timers/promises');

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

// Assert recursive watch does not leak handles
const rootDirectory = fs.mkdtempSync(testDir + path.sep);
const testDirectory = path.join(rootDirectory, 'test-7');
const filePath = path.join(testDirectory, 'only-file.txt');
fs.mkdirSync(testDirectory);

let watcherClosed = false;
const watcher = fs.watch(testDirectory, { recursive: true });
watcher.on('change', common.mustCallAtLeast(async (event, filename) => {
  await setTimeout(common.platformTimeout(100));
  if (filename === path.basename(filePath)) {
    watcher.close();
    watcherClosed = true;
  }
  await setTimeout(common.platformTimeout(100));
  assert(!process._getActiveHandles().some((handle) => handle.constructor.name === 'StatWatcher'));
}));

process.on('exit', function() {
  assert(watcherClosed, 'watcher Object was not closed');
});

// Do the write with a delay to ensure that the OS is ready to notify us.
(async () => {
  await setTimeout(200);
  fs.writeFileSync(filePath, 'content');
})().then(common.mustCall());
