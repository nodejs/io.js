'use strict';

// This tests that process.cwd() is accurate when
// restoring state from a snapshot

require('../common');
const { spawnSyncAndAssert } = require('../common/child_process');
const tmpdir = require('../common/tmpdir');
const fixtures = require('../common/fixtures');
const fs = require('fs');

tmpdir.refresh();
const blobPath = tmpdir.resolve('snapshot.blob');
const file = fixtures.path('snapshot', 'cwd.js');

const subdir = tmpdir.resolve('foo');
fs.mkdirSync(subdir);

{
  // Create the snapshot.
  spawnSyncAndAssert(process.execPath, [
    '--snapshot-blob',
    blobPath,
    '--build-snapshot',
    file,
  ], {
    cwd: tmpdir.path,
    encoding: 'utf8'
  }, {
    status: 0,
  });
}

{
  spawnSyncAndAssert(process.execPath, [
    '--snapshot-blob',
    blobPath,
    file,
  ], {
    cwd: subdir,
    encoding: 'utf8'
  }, {
    status: 0,
    trim: true,
    stdout: subdir,
  });
}
