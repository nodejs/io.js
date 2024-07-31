'use strict';

// This tests that user land snapshots works when the instance restored from
// the snapshot is launched with --help, --check

require('../common');
const assert = require('assert');
const tmpdir = require('../common/tmpdir');
const fixtures = require('../common/fixtures');
const {
  spawnSyncAndAssert,
  spawnSyncAndExit,
  spawnSyncAndExitWithoutError,
} = require('../common/child_process');
const fs = require('fs');

tmpdir.refresh();

let snapshotScript = 'node:embedded_snapshot_main';
if (!process.config.variables.node_use_node_snapshot) {
  // Check that Node.js built without an embedded snapshot
  // exits with 9 when node:embedded_snapshot_main is specified
  // as snapshot entry point.
  spawnSyncAndExit(process.execPath, [
    '--build-snapshot',
    snapshotScript,
  ], {
    cwd: tmpdir.path
  }, {
    status: 9,
    signal: null,
    stderr: /Node\.js was built without embedded snapshot/
  });

  snapshotScript = fixtures.path('empty.js');
}

// By default, the snapshot blob path is cwd/snapshot.blob.
{
  // Create the snapshot.
  spawnSyncAndExitWithoutError(process.execPath, [
    '--build-snapshot',
    snapshotScript,
  ], {
    cwd: tmpdir.path
  });
  const stats = fs.statSync(tmpdir.resolve('snapshot.blob'));
  assert(stats.isFile());
}

tmpdir.refresh();
const blobPath = tmpdir.resolve('my-snapshot.blob');
{
  // Create the snapshot.
  spawnSyncAndExitWithoutError(process.execPath, [
    '--snapshot-blob',
    blobPath,
    '--build-snapshot',
    snapshotScript,
  ], {
    cwd: tmpdir.path
  });
  const stats = fs.statSync(blobPath);
  assert(stats.isFile());
}

{
  // Check --help.
  spawnSyncAndAssert(process.execPath, [
    '--snapshot-blob',
    blobPath,
    '--help',
  ], {
    cwd: tmpdir.path
  }, {
    stdout: /--help/
  });
}

{
  // Check -c.
  spawnSyncAndAssert(process.execPath, [
    '--snapshot-blob',
    blobPath,
    '-c',
    fixtures.path('snapshot', 'marked.js'),
  ], {
    cwd: tmpdir.path
  }, {
    stderr: '',
    stdout: '',
    trim: true
  });
}
