'use strict';

// This tests NODE_COMPILE_CACHE works in existing directory.

require('../common');
const { spawnSyncAndAssert } = require('../common/child_process');
const assert = require('assert');
const tmpdir = require('../common/tmpdir');
const fixtures = require('../common/fixtures');
const fs = require('fs');

function testDisallowed(dummyDir, cacheDirInPermission, cacheDirInEnv) {
  console.log(dummyDir, cacheDirInPermission, cacheDirInEnv);  // Logging for debugging.

  tmpdir.refresh();
  const script = tmpdir.resolve(dummyDir, 'empty.js');
  fs.mkdirSync(tmpdir.resolve(dummyDir));
  fs.copyFileSync(fixtures.path('empty.js'), script);
  // If the directory doesn't exist, permission will just be disallowed.
  if (cacheDirInPermission !== '*') {
    fs.mkdirSync(tmpdir.resolve(cacheDirInPermission));
  }

  spawnSyncAndAssert(
    process.execPath,
    [
      '--permission',
      `--allow-fs-read=${dummyDir}`,  // No read or write permission for cache dir.
      `--allow-fs-write=${dummyDir}`,
      script,
    ],
    {
      env: {
        ...process.env,
        NODE_DEBUG_NATIVE: 'COMPILE_CACHE',
        NODE_COMPILE_CACHE: `${cacheDirInEnv}`
      },
      cwd: tmpdir.path
    },
    {
      stderr(output) {
        assert.match(output, /Skipping compile cache because write permission for .* is not granted/);
        return true;
      }
    });

  spawnSyncAndAssert(
    process.execPath,
    [
      '--permission',
      `--allow-fs-read=${dummyDir}`,
      `--allow-fs-read=${cacheDirInPermission}`,  // Read-only
      `--allow-fs-write=${dummyDir}`,
      script,
    ],
    {
      env: {
        ...process.env,
        NODE_DEBUG_NATIVE: 'COMPILE_CACHE',
        NODE_COMPILE_CACHE: `${cacheDirInEnv}`
      },
      cwd: tmpdir.path
    },
    {
      stderr(output) {
        assert.match(output, /Skipping compile cache because write permission for .* is not granted/);
        return true;
      }
    });

  spawnSyncAndAssert(
    process.execPath,
    [
      '--permission',
      `--allow-fs-read=${dummyDir}`,
      `--allow-fs-write=${cacheDirInPermission}`,  // Write-only
      script,
    ],
    {
      env: {
        ...process.env,
        NODE_DEBUG_NATIVE: 'COMPILE_CACHE',
        NODE_COMPILE_CACHE: `${cacheDirInEnv}`
      },
      cwd: tmpdir.path
    },
    {
      stderr(output) {
        assert.match(output, /Skipping compile cache because read permission for .* is not granted/);
        return true;
      }
    });
}

{
  testDisallowed(tmpdir.resolve('dummy'), tmpdir.resolve('.compile_cache') + '/', '.compile_cache');
  testDisallowed(tmpdir.resolve('dummy'), tmpdir.resolve('.compile_cache/') + '/', tmpdir.resolve('.compile_cache'));
  testDisallowed(tmpdir.resolve('dummy'), '*', '.compile_cache');
  testDisallowed(tmpdir.resolve('dummy'), '*', tmpdir.resolve('.compile_cache'));
}
