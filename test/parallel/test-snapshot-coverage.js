'use strict';

// This tests that the snapshot works with built-in coverage collection.

const common = require('../common');
common.skipIfInspectorDisabled();

const { spawnSyncAndExitWithoutError } = require('../common/child_process');
const tmpdir = require('../common/tmpdir');
const fixtures = require('../common/fixtures');
const fs = require('fs');
const assert = require('assert');

tmpdir.refresh();
const blobPath = tmpdir.resolve('snapshot.blob');
const file = fixtures.path('empty.js');

function filterCoverageFiles(name) {
  return name.startsWith('coverage') && name.endsWith('.json');
}
{
  // Create the snapshot.
  const { child } = spawnSyncAndExitWithoutError(process.execPath, [
    '--snapshot-blob',
    blobPath,
    '--build-snapshot',
    file,
  ], {
    cwd: tmpdir.path,
    env: {
      ...process.env,
      NODE_V8_COVERAGE: tmpdir.path,
      NODE_DEBUG_NATIVE: 'inspector_profiler',
    }
  });
  const files = fs.readdirSync(tmpdir.path);
  console.log('Files in tmpdir.path', files);  // Log for debugging the test.
  const coverage = files.filter(filterCoverageFiles);
  console.log(child.stderr.toString());
  assert.strictEqual(coverage.length, 1);
}

{
  const { child } = spawnSyncAndExitWithoutError(process.execPath, [
    '--snapshot-blob',
    blobPath,
    file,
  ], {
    cwd: tmpdir.path,
    env: {
      ...process.env,
      NODE_V8_COVERAGE: tmpdir.path,
      NODE_DEBUG_NATIVE: 'inspector_profiler',
    },
  });
  const files = fs.readdirSync(tmpdir.path);
  console.log('Files in tmpdir.path', files);  // Log for debugging the test.
  const coverage = files.filter(filterCoverageFiles);
  console.log(child.stderr.toString());
  assert.strictEqual(coverage.length, 2);
}
