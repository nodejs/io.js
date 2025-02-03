'use strict';

// This tests module.enableCompileCache() and module.getCompileCacheDir() work
// with a NODE_COMPILE_CACHE environment variable override.

require('../common');
const { spawnSyncAndAssert } = require('../common/child_process');
const assert = require('assert');
const fixtures = require('../common/fixtures');
const tmpdir = require('../common/tmpdir');
const fs = require('fs');

{
  // Test that it works with non-existent directory.
  tmpdir.refresh();
  const apiDir = tmpdir.resolve('api_dir');
  const envDir = tmpdir.resolve('env_dir');
  spawnSyncAndAssert(
    process.execPath,
    ['-r', fixtures.path('compile-cache-wrapper.js'), fixtures.path('empty.js')],
    {
      env: {
        ...process.env,
        NODE_DEBUG_NATIVE: 'COMPILE_CACHE',
        NODE_COMPILE_CACHE: envDir,
        NODE_TEST_COMPILE_CACHE_DIR: apiDir,
      },
      cwd: tmpdir.path
    },
    {
      stdout(output) {
        console.log(output);  // Logging for debugging.
        assert.match(output, /dir before enableCompileCache: .*env_dir/);
        assert.match(output, /Compile cache already enabled/);
        assert.match(output, /dir after enableCompileCache: .*env_dir/);
        return true;
      },
      stderr(output) {
        console.log(output);  // Logging for debugging.
        assert.match(output, /reading cache from .*env_dir.* for CommonJS .*empty\.js/);
        assert.match(output, /empty\.js was not initialized, initializing the in-memory entry/);
        assert.match(output, /writing cache for .*empty\.js.*success/);
        return true;
      }
    });

  const cacheDir = fs.readdirSync(tmpdir.path);
  assert.strictEqual(cacheDir.length, 1);
  const entries = fs.readdirSync(tmpdir.resolve(cacheDir[0]));
  assert.strictEqual(entries.length, 1);

  // Second run reads the cache, but no need to re-write because it didn't change.
  spawnSyncAndAssert(
    process.execPath,
    ['-r', fixtures.path('compile-cache-wrapper.js'), fixtures.path('empty.js')],
    {
      env: {
        ...process.env,
        NODE_DEBUG_NATIVE: 'COMPILE_CACHE',
        NODE_COMPILE_CACHE: envDir,
        NODE_TEST_COMPILE_CACHE_DIR: apiDir,
      },
      cwd: tmpdir.path
    },
    {
      stdout(output) {
        console.log(output);  // Logging for debugging.
        assert.match(output, /dir before enableCompileCache: .*env_dir/);
        assert.match(output, /Compile cache already enabled/);
        assert.match(output, /dir after enableCompileCache: .*env_dir/);
        return true;
      },
      stderr(output) {
        console.log(output);  // Logging for debugging.
        assert.match(output, /reading cache from .*env_dir.* for CommonJS .*empty\.js/);
        assert.match(output, /cache for .*empty\.js was accepted, keeping the in-memory entry/);
        assert.match(output, /.*skip .*empty\.js because cache was the same/);
        return true;
      }
    });
}
