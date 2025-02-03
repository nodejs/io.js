'use strict';

// This tests module.enableCompileCache() and module.getCompileCacheDir() work with
// the TMPDIR environment variable override.

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

  spawnSyncAndAssert(
    process.execPath,
    ['-r', fixtures.path('compile-cache-wrapper.js'), fixtures.path('empty.js')],
    {
      env: {
        ...process.env,
        NODE_DEBUG_NATIVE: 'COMPILE_CACHE',
        NODE_COMPILE_CACHE: undefined,
        NODE_TEST_COMPILE_CACHE_DIR: undefined,
        // TMPDIR is ignored on Windows while on other platforms, TMPDIR takes precedence.
        // Override all related environment variables to ensure the tmpdir is configured properly
        // regardless of the global environment variables used to run the test.
        TMPDIR: tmpdir.path,
        TEMP: tmpdir.path,
        TMP: tmpdir.path,
      },
      cwd: tmpdir.path
    },
    {
      stdout(output) {
        console.log(output);  // Logging for debugging.
        assert.match(output, /dir before enableCompileCache: undefined/);
        assert.match(output, /Compile cache enabled\. .*node-compile-cache/);
        assert.match(output, /dir after enableCompileCache: .*node-compile-cache/);
        return true;
      },
      stderr(output) {
        console.log(output);  // Logging for debugging.
        assert.match(output, /empty\.js was not initialized, initializing the in-memory entry/);
        assert.match(output, /writing cache for .*empty\.js.*success/);
        return true;
      }
    });

  const baseDir = path.join(tmpdir.path, 'node-compile-cache');
  const cacheDir = fs.readdirSync(baseDir);
  assert.strictEqual(cacheDir.length, 1);
  const entries = fs.readdirSync(path.join(baseDir, cacheDir[0]));
  assert.strictEqual(entries.length, 1);

  // Second run reads the cache, but no need to re-write because it didn't change.
  spawnSyncAndAssert(
    process.execPath,
    ['-r', fixtures.path('compile-cache-wrapper.js'), fixtures.path('empty.js')],
    {
      env: {
        ...process.env,
        NODE_DEBUG_NATIVE: 'COMPILE_CACHE',
        NODE_COMPILE_CACHE: undefined,
        NODE_TEST_COMPILE_CACHE_DIR: undefined,
        // TMPDIR is ignored on Windows while on other platforms, TMPDIR takes precedence.
        // Override all related environment variables to ensure the tmpdir is configured properly
        // regardless of the global environment variables used to run the test.
        TMPDIR: tmpdir.path,
        TEMP: tmpdir.path,
        TMP: tmpdir.path,
      },
      cwd: tmpdir.path
    },
    {
      stdout(output) {
        console.log(output);  // Logging for debugging.
        assert.match(output, /dir before enableCompileCache: undefined/);
        assert.match(output, /Compile cache enabled\. .*node-compile-cache/);
        assert.match(output, /dir after enableCompileCache: .*node-compile-cache/);
        return true;
      },
      stderr(output) {
        console.log(output);  // Logging for debugging.
        assert.match(output, /cache for .*empty\.js was accepted, keeping the in-memory entry/);
        assert.match(output, /.*skip .*empty\.js because cache was the same/);
        return true;
      }
    });
}
