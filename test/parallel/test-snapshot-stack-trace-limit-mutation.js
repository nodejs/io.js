'use strict';

// This tests mutation to Error.stackTraceLimit in both the snapshot builder script
// and the snapshot main script work.

require('../common');
const assert = require('assert');
const tmpdir = require('../common/tmpdir');
const fixtures = require('../common/fixtures');
const { spawnSyncAndAssert, spawnSyncAndExitWithoutError } = require('../common/child_process');

const blobPath = tmpdir.resolve('snapshot.blob');

function test(additionalArguments = [], additionalEnv = {}) {
  tmpdir.refresh();
  // Check the mutation works without --stack-trace-limit.
  spawnSyncAndAssert(process.execPath, [
    ...additionalArguments,
    '--snapshot-blob',
    blobPath,
    '--build-snapshot',
    fixtures.path('snapshot', 'mutate-error-stack-trace-limit.js'),
  ], {
    cwd: tmpdir.path,
    env: {
      ...process.env,
      ...additionalEnv,
    }
  }, {
    stderr(output) {
      assert.match(output, /Error\.stackTraceLimit has been modified by the snapshot builder script/);
      assert.match(output, /It will be preserved after snapshot deserialization/);
    }
  });
  spawnSyncAndExitWithoutError(process.execPath, [
    '--snapshot-blob',
    blobPath,
  ], {
    cwd: tmpdir.path
  });
}

test();
test([], { TEST_IN_SERIALIZER: 1 });
test(['--stack-trace-limit=50']);
test(['--stack-trace-limit=50'], { TEST_IN_SERIALIZER: 1 });
