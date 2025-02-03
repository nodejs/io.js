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

(async () => {
  // Add a folder to already watching folder

  const rootDirectory = fs.mkdtempSync(testDir + path.sep);
  const testDirectory = path.join(rootDirectory, 'test-2');
  fs.mkdirSync(testDirectory);

  const testFile = path.join(testDirectory, 'folder-2');

  const watcher = fs.watch(testDirectory, { recursive: true });
  let watcherClosed = false;
  watcher.on('change', function(event, filename) {
    if (filename === path.basename(testFile)) {
      assert.strictEqual(event, 'rename');
      watcher.close();
      watcherClosed = true;
    }
  });

  await setTimeout(common.platformTimeout(100));
  fs.mkdirSync(testFile);

  process.once('exit', function() {
    assert(watcherClosed, 'watcher Object was not closed');
  });
})().then(common.mustCall());
