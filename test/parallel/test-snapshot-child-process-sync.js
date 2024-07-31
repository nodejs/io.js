'use strict';

// This tests that process.cwd() is accurate when
// restoring state from a snapshot

require('../common');
const { spawnSyncAndAssert } = require('../common/child_process');
const tmpdir = require('../common/tmpdir');
const fixtures = require('../common/fixtures');
const assert = require('assert');

tmpdir.refresh();
const blobPath = tmpdir.resolve('snapshot.blob');
const file = fixtures.path('snapshot', 'child-process-sync.js');
const expected = [
  'From child process spawnSync',
  'From child process execSync',
  'From child process execFileSync',
];

{
  // Create the snapshot.
  spawnSyncAndAssert(process.execPath, [
    '--snapshot-blob',
    blobPath,
    '--build-snapshot',
    file,
  ], {
    cwd: tmpdir.path,
  }, {
    trim: true,
    stdout(output) {
      assert.deepStrictEqual(output.split('\n'), expected);
      return true;
    }
  });
}

{
  spawnSyncAndAssert(process.execPath, [
    '--snapshot-blob',
    blobPath,
    file,
  ], {
    cwd: tmpdir.path,
  }, {
    trim: true,
    stdout(output) {
      assert.deepStrictEqual(output.split('\n'), expected);
      return true;
    }
  });
}
