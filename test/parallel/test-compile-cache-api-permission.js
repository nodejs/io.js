'use strict';

// This tests module.enableCompileCache() works with the permission model.

require('../common');
const { spawnSyncAndAssert } = require('../common/child_process');
const assert = require('assert');
const fixtures = require('../common/fixtures');
const tmpdir = require('../common/tmpdir');
const fs = require('fs');

{
  tmpdir.refresh();
  const cacheDir = tmpdir.resolve('compile-cache');
  const scriptDir = tmpdir.resolve('scripts');
  // If the directory doesn't exist, permission will just be disallowed.
  fs.mkdirSync(cacheDir);
  fs.mkdirSync(scriptDir);

  const empty = tmpdir.resolve('scripts', 'empty.js');
  const wrapper = tmpdir.resolve('scripts', 'compile-cache-wrapper.js');

  fs.copyFileSync(fixtures.path('empty.js'), empty);
  fs.copyFileSync(fixtures.path('compile-cache-wrapper.js'), wrapper);

  spawnSyncAndAssert(
    process.execPath,
    [
      '--permission', `--allow-fs-read=${scriptDir}`, `--allow-fs-write=${scriptDir}`,
      '-r', wrapper, empty,
    ],
    {
      env: {
        ...process.env,
        NODE_DEBUG_NATIVE: 'COMPILE_CACHE',
        NODE_COMPILE_CACHE: undefined,
        NODE_TEST_COMPILE_CACHE_DIR: cacheDir,
      },
      cwd: tmpdir.path
    },
    {
      stdout(output) {
        console.log(output);  // Logging for debugging.
        assert.match(output, /dir before enableCompileCache: undefined/);
        assert.match(output, /Compile cache failed/);
        assert.match(output, /Skipping compile cache because write permission for .* is not granted/);
        assert.match(output, /dir after enableCompileCache: undefined/);
        return true;
      },
      stderr(output) {
        console.log(output);  // Logging for debugging.
        assert.match(output, /Skipping compile cache because write permission for .* is not granted/);
        return true;
      }
    });
}
