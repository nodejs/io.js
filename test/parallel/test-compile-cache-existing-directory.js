'use strict';

// This tests NODE_COMPILE_CACHE works in existing directory.

require('../common');
const { spawnSyncAndAssert } = require('../common/child_process');
const assert = require('assert');
const fs = require('fs');
const fixtures = require('../common/fixtures');
const tmpdir = require('../common/tmpdir');

{
  tmpdir.refresh();
  const dir = tmpdir.resolve('.compile_cache_dir');
  fs.mkdirSync(dir);

  spawnSyncAndAssert(
    process.execPath,
    [fixtures.path('empty.js')],
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
        assert.match(output, /empty\.js was not initialized, initializing the in-memory entry/);
        assert.match(output, /writing cache for .*empty\.js.*success/);
        return true;
      }
    });

  // Second run reads the cache.
  spawnSyncAndAssert(
    process.execPath,
    [fixtures.path('empty.js')],
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
        assert.match(output, /cache for .*empty\.js was accepted, keeping the in-memory entry/);
        assert.match(output, /.*skip .*empty\.js because cache was the same/);
        return true;
      }
    });
}
