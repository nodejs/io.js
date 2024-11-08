'use strict';

// This tests Error.stackTraceLimit is fixed up for snapshot-building contexts,
// and can be restored after deserialization.

require('../common');
const assert = require('assert');
const tmpdir = require('../common/tmpdir');
const fixtures = require('../common/fixtures');
const { spawnSyncAndAssert } = require('../common/child_process');

tmpdir.refresh();
const blobPath = tmpdir.resolve('snapshot.blob');
{
  spawnSyncAndAssert(process.execPath, [
    '--stack-trace-limit=50',
    '--snapshot-blob',
    blobPath,
    '--build-snapshot',
    fixtures.path('snapshot', 'error-stack.js'),
  ], {
    cwd: tmpdir.path
  }, {
    stdout(output) {
      assert.match(output, /During snapshot building, Error\.stackTraceLimit = 50/);
      const matches = [...output.matchAll(/at recurse/g)];
      assert.strictEqual(matches.length, 30);
    }
  });
}

{
  spawnSyncAndAssert(process.execPath, [
    '--stack-trace-limit=20',
    '--snapshot-blob',
    blobPath,
  ], {
    cwd: tmpdir.path
  }, {
    stdout(output) {
      assert.match(output, /After snapshot deserialization, Error\.stackTraceLimit = 20/);
      const matches = [...output.matchAll(/at recurse/g)];
      assert.strictEqual(matches.length, 20);
    }
  });
}
