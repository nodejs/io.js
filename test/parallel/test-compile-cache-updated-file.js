'use strict';

// This tests NODE_COMPILE_CACHE works in existing directory.

require('../common');
const { spawnSyncAndAssert } = require('../common/child_process');
const assert = require('assert');
const fs = require('fs');
const tmpdir = require('../common/tmpdir');

{
  tmpdir.refresh();
  const dir = tmpdir.resolve('.compile_cache_dir');
  const script = tmpdir.resolve('script.js');
  fs.writeFileSync(script, 'const foo = 1;', 'utf-8');

  spawnSyncAndAssert(
    process.execPath,
    [script],
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
        assert.match(output, /script\.js was not initialized, initializing the in-memory entry/);
        assert.match(output, /writing cache for .*script\.js.*success/);
        return true;
      }
    });

  // Update the content.
  fs.writeFileSync(script, 'const foo = 2;', 'utf-8');

  // Another run regenerates it.
  spawnSyncAndAssert(
    process.execPath,
    [script],
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
        assert.match(output, /reading cache from .* for .*script\.js.*code hash mismatch:/);
        assert.match(output, /writing cache for .*script\.js.*success/);
        return true;
      }
    });

  // Then it's consumed just fine.
  spawnSyncAndAssert(
    process.execPath,
    [script],
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
        assert.match(output, /cache for .*script\.js was accepted, keeping the in-memory entry/);
        assert.match(output, /.*skip .*script\.js because cache was the same/);
        return true;
      }
    });
}
