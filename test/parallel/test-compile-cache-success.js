'use strict';

// This tests NODE_COMPILE_CACHE works.

require('../common');
const { spawnSyncAndAssert } = require('../common/child_process');
const assert = require('assert');
const fixtures = require('../common/fixtures');
const tmpdir = require('../common/tmpdir');
const fs = require('fs');
const path = require('path');

{
  // Test that it works with non-existent directory.
  tmpdir.refresh();
  const dir = tmpdir.resolve('.compile_cache_dir');

  spawnSyncAndAssert(
    process.execPath,
    [fixtures.path('snapshot', 'typescript.js')],
    {
      env: {
        ...process.env,
        NODE_DEBUG_NATIVE: 'COMPILE_CACHE',
        NODE_COMPILE_CACHE: dir
      },
      cwd: tmpdir.path
    },
    {
      stderr(output) {
        console.log(output);  // Logging for debugging.
        assert.match(output, /typescript\.js was not initialized, initializing the in-memory entry/);
        assert.match(output, /writing cache for .*typescript\.js.*success/);
        return true;
      }
    });

  const cacheDir = fs.readdirSync(dir);
  assert.strictEqual(cacheDir.length, 1);
  const entries = fs.readdirSync(path.join(dir, cacheDir[0]));
  assert.strictEqual(entries.length, 1);
  const cacheFile = path.join(dir, cacheDir[0], entries[0]);
  const data = fs.readFileSync(cacheFile);
  console.log(`Header in ${cacheFile}`, new Uint32Array(data.buffer, data.byteOffset, 4));

  // Second run reads the cache, but no need to re-write because it didn't change.
  spawnSyncAndAssert(
    process.execPath,
    [fixtures.path('snapshot', 'typescript.js')],
    {
      env: {
        ...process.env,
        NODE_DEBUG_NATIVE: 'COMPILE_CACHE',
        NODE_COMPILE_CACHE: dir
      },
      cwd: tmpdir.path
    },
    {
      stderr(output) {
        console.log(output);  // Logging for debugging.
        assert.match(output, /cache for .*typescript\.js was accepted, keeping the in-memory entry/);
        assert.match(output, /.*skip .*typescript\.js because cache was the same/);
        return true;
      }
    });
}
