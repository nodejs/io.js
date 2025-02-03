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
  // Add a recursive symlink to the parent folder

  const testDirectory = fs.mkdtempSync(testDir + path.sep);

  // Do not use `testDirectory` as base. It will hang the tests.
  const rootDirectory = path.join(testDirectory, 'test-1');
  fs.mkdirSync(rootDirectory);

  const filePath = path.join(rootDirectory, 'file.txt');

  const symlinkFolder = path.join(rootDirectory, 'symlink-folder');
  fs.symlinkSync(rootDirectory, symlinkFolder);

  if (common.isMacOS) {
    // On macOS delay watcher start to avoid leaking previous events.
    // Refs: https://github.com/libuv/libuv/pull/4503
    await setTimeout(common.platformTimeout(100));
  }

  const watcher = fs.watch(rootDirectory, { recursive: true });
  let watcherClosed = false;
  watcher.on('change', function(event, filename) {
    assert.ok(event === 'rename', `Received ${event}`);
    assert.ok(filename === path.basename(symlinkFolder) || filename === path.basename(filePath), `Received ${filename}`);

    if (filename === path.basename(filePath)) {
      watcher.close();
      watcherClosed = true;
    }
  });

  await setTimeout(common.platformTimeout(100));
  fs.writeFileSync(filePath, 'world');

  process.once('exit', function() {
    assert(watcherClosed, 'watcher Object was not closed');
  });
})().then(common.mustCall());

(async () => {
  // This test checks how a symlink to outside the tracking folder can trigger change
  // tmp/sub-directory/tracking-folder/symlink-folder -> tmp/sub-directory

  const rootDirectory = fs.mkdtempSync(testDir + path.sep);

  const subDirectory = path.join(rootDirectory, 'sub-directory');
  fs.mkdirSync(subDirectory);

  const trackingSubDirectory = path.join(subDirectory, 'tracking-folder');
  fs.mkdirSync(trackingSubDirectory);

  const symlinkFolder = path.join(trackingSubDirectory, 'symlink-folder');
  fs.symlinkSync(subDirectory, symlinkFolder);

  const forbiddenFile = path.join(subDirectory, 'forbidden.txt');
  const acceptableFile = path.join(trackingSubDirectory, 'acceptable.txt');

  if (common.isMacOS) {
    // On macOS delay watcher start to avoid leaking previous events.
    // Refs: https://github.com/libuv/libuv/pull/4503
    await setTimeout(common.platformTimeout(100));
  }

  const watcher = fs.watch(trackingSubDirectory, { recursive: true });
  let watcherClosed = false;
  watcher.on('change', function(event, filename) {
    // macOS will only change the following events:
    // { event: 'rename', filename: 'symlink-folder' }
    // { event: 'rename', filename: 'acceptable.txt' }
    assert.ok(event === 'rename', `Received ${event}`);
    assert.ok(filename === path.basename(symlinkFolder) || filename === path.basename(acceptableFile), `Received ${filename}`);

    if (filename === path.basename(acceptableFile)) {
      watcher.close();
      watcherClosed = true;
    }
  });

  await setTimeout(common.platformTimeout(100));
  fs.writeFileSync(forbiddenFile, 'world');
  await setTimeout(common.platformTimeout(100));
  fs.writeFileSync(acceptableFile, 'acceptable');

  process.once('exit', function() {
    assert(watcherClosed, 'watcher Object was not closed');
  });
})().then(common.mustCall());
