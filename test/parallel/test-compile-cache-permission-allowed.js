'use strict';

// This tests NODE_COMPILE_CACHE works in existing directory.

require('../common');
const { spawnSyncAndAssert } = require('../common/child_process');
const assert = require('assert');
const tmpdir = require('../common/tmpdir');
const fixtures = require('../common/fixtures');
const fs = require('fs');

function testAllowed(readDir, writeDir, envDir) {
  console.log(readDir, writeDir, envDir);  // Logging for debugging.

  tmpdir.refresh();
  const dummyDir = tmpdir.resolve('dummy');
  fs.mkdirSync(dummyDir);
  const script = tmpdir.resolve(dummyDir, 'empty.js');
  fs.copyFileSync(fixtures.path('empty.js'), script);
  // If the directory doesn't exist, permission will just be disallowed.
  fs.mkdirSync(tmpdir.resolve(envDir));

  spawnSyncAndAssert(
    process.execPath,
    [
      '--permission',
      `--allow-fs-read=${dummyDir}`,
      `--allow-fs-read=${readDir}`,
      `--allow-fs-write=${writeDir}`,
      script,
    ],
    {
      env: {
        ...process.env,
        NODE_DEBUG_NATIVE: 'COMPILE_CACHE',
        NODE_COMPILE_CACHE: `${envDir}`
      },
      cwd: tmpdir.path
    },
    {
      stderr(output) {
        assert.match(output, /writing cache for .*empty\.js.*success/);
        return true;
      }
    });

  spawnSyncAndAssert(
    process.execPath,
    [
      '--permission',
      `--allow-fs-read=${dummyDir}`,
      `--allow-fs-read=${readDir}`,
      `--allow-fs-write=${writeDir}`,
      script,
    ],
    {
      env: {
        ...process.env,
        NODE_DEBUG_NATIVE: 'COMPILE_CACHE',
        NODE_COMPILE_CACHE: `${envDir}`
      },
      cwd: tmpdir.path
    },
    {
      stderr(output) {
        assert.match(output, /cache for .*empty\.js was accepted/);
        return true;
      }
    });
}

{
  testAllowed(tmpdir.resolve('.compile_cache'), tmpdir.resolve('.compile_cache'), '.compile_cache');
  testAllowed(tmpdir.resolve('.compile_cache'), tmpdir.resolve('.compile_cache'), tmpdir.resolve('.compile_cache'));
  testAllowed('*', '*', '.compile_cache');
  testAllowed('*', tmpdir.resolve('.compile_cache'), '.compile_cache');
  testAllowed(tmpdir.resolve('.compile_cache'), '*', '.compile_cache');
}
