'use strict';

// This tests that the warning handler is cleaned up properly
// during snapshot serialization and installed again during
// deserialization.

require('../common');

const assert = require('assert');
const tmpdir = require('../common/tmpdir');
const fixtures = require('../common/fixtures');
const { spawnSyncAndAssert, spawnSyncAndExitWithoutError } = require('../common/child_process');
const fs = require('fs');

const warningScript = fixtures.path('snapshot', 'warning.js');
const blobPath = tmpdir.resolve('snapshot.blob');
const empty = fixtures.path('empty.js');

tmpdir.refresh();
{
  console.log('\n# Check snapshot scripts that do not emit warnings.');
  spawnSyncAndExitWithoutError(process.execPath, [
    '--snapshot-blob',
    blobPath,
    '--build-snapshot',
    empty,
  ], {
    cwd: tmpdir.path
  });
  const stats = fs.statSync(blobPath);
  assert(stats.isFile());

  spawnSyncAndAssert(process.execPath, [
    '--snapshot-blob',
    blobPath,
    warningScript,
  ], {
    cwd: tmpdir.path
  }, {
    stderr(output) {
      const match = output.match(/Warning: test warning/g);
      assert.strictEqual(match.length, 1);
      return true;
    }
  });
}

tmpdir.refresh();
{
  console.log('\n# Check snapshot scripts that emit ' +
              'warnings and --trace-warnings hint.');
  spawnSyncAndAssert(process.execPath, [
    '--snapshot-blob',
    blobPath,
    '--build-snapshot',
    warningScript,
  ], {
    cwd: tmpdir.path
  }, {
    stderr(output) {
      let match = output.match(/Warning: test warning/g);
      assert.strictEqual(match.length, 1);
      match = output.match(/Use `node --trace-warnings/g);
      assert.strictEqual(match.length, 1);
      return true;
    }
  });
  const stats = fs.statSync(blobPath);
  assert(stats.isFile());

  spawnSyncAndAssert(process.execPath, [
    '--snapshot-blob',
    blobPath,
    warningScript,
  ], {
    cwd: tmpdir.path
  }, {
    stderr(output) {
      // Warnings should not be handled more than once.
      let match = output.match(/Warning: test warning/g);
      assert.strictEqual(match.length, 1);
      match = output.match(/Use `node --trace-warnings/g);
      assert.strictEqual(match.length, 1);
      return true;
    }
  });
}

tmpdir.refresh();
{
  console.log('\n# Check --redirect-warnings');
  const warningFile1 = tmpdir.resolve('warnings.txt');
  const warningFile2 = tmpdir.resolve('warnings2.txt');

  spawnSyncAndAssert(process.execPath, [
    '--snapshot-blob',
    blobPath,
    '--redirect-warnings',
    warningFile1,
    '--build-snapshot',
    warningScript,
  ], {
    cwd: tmpdir.path
  }, {
    stderr(output) {
      assert.doesNotMatch(output, /Warning: test warning/);
    }
  });

  const stats = fs.statSync(blobPath);
  assert(stats.isFile());

  const warnings1 = fs.readFileSync(warningFile1, 'utf8');
  console.log(warningFile1, ':', warnings1);
  let match = warnings1.match(/Warning: test warning/g);
  assert.strictEqual(match.length, 1);
  match = warnings1.match(/Use `node --trace-warnings/g);
  assert.strictEqual(match.length, 1);
  fs.rmSync(warningFile1, {
    maxRetries: 3, recursive: false, force: true
  });

  spawnSyncAndAssert(process.execPath, [
    '--snapshot-blob',
    blobPath,
    '--redirect-warnings',
    warningFile2,
    warningScript,
  ], {
    cwd: tmpdir.path
  }, {
    stderr(output) {
      assert.doesNotMatch(output, /Warning: test warning/);
      return true;
    }
  });
  assert(!fs.existsSync(warningFile1));

  const warnings2 = fs.readFileSync(warningFile2, 'utf8');
  console.log(warningFile2, ':', warnings1);
  match = warnings2.match(/Warning: test warning/g);
  assert.strictEqual(match.length, 1);
  match = warnings2.match(/Use `node --trace-warnings/g);
  assert.strictEqual(match.length, 1);
}
