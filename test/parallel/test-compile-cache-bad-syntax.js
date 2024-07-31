'use strict';

// This tests NODE_COMPILE_CACHE works.

require('../common');
const { spawnSyncAndExit } = require('../common/child_process');
const fixtures = require('../common/fixtures');
const tmpdir = require('../common/tmpdir');
const assert = require('assert');
const fs = require('fs');
const path = require('path');

{
  // Test that it throws if the script fails to parse, and no cache is created.
  tmpdir.refresh();
  const dir = tmpdir.resolve('.compile_cache_dir');

  spawnSyncAndExit(
    process.execPath,
    [fixtures.path('syntax', 'bad_syntax.js')],
    {
      env: {
        ...process.env,
        NODE_DEBUG_NATIVE: 'COMPILE_CACHE',
        NODE_COMPILE_CACHE: dir
      },
      cwd: tmpdir.path
    },
    {
      status: 1,
      stderr: /skip .*bad_syntax\.js because the cache was not initialized/,
    });

  const cacheDir = fs.readdirSync(dir);
  assert.strictEqual(cacheDir.length, 1);
  const entries = fs.readdirSync(path.join(dir, cacheDir[0]));
  assert.strictEqual(entries.length, 0);

  spawnSyncAndExit(
    process.execPath,
    [fixtures.path('syntax', 'bad_syntax.mjs')],
    {
      env: {
        ...process.env,
        NODE_DEBUG_NATIVE: 'COMPILE_CACHE',
        NODE_COMPILE_CACHE: dir
      },
      cwd: tmpdir.path
    },
    {
      status: 1,
      stderr: /skip .*bad_syntax\.mjs because the cache was not initialized/,
    });
}
