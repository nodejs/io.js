'use strict';

// This tests NODE_COMPILE_CACHE works in existing directory.

require('../common');
const { spawnSyncAndAssert } = require('../common/child_process');
const assert = require('assert');
const tmpdir = require('../common/tmpdir');
const fixtures = require('../common/fixtures');

{
  tmpdir.refresh();
  const dir = tmpdir.resolve('.compile_cache_dir');
  const cjs = fixtures.path('es-modules', 'dynamic-import', 'import.cjs');
  const mjs = fixtures.path('es-modules', 'dynamic-import', 'import.mjs');

  spawnSyncAndAssert(
    process.execPath,
    [cjs],
    {
      env: {
        ...process.env,
        NODE_DEBUG_NATIVE: 'COMPILE_CACHE',
        NODE_COMPILE_CACHE: dir
      },
      cwd: tmpdir.path
    },
    {
      trim: true,
      stdout: 'hello world',
      stderr(output) {
        assert.match(output, /reading cache from .* for ESM .*mod\.js/);
        assert.match(output, /mod\.js was not initialized, initializing the in-memory entry/);
        assert.match(output, /writing cache for .*mod\.js.*success/);
        assert.match(output, /reading cache .* for CommonJS .*import\.cjs/);
        assert.match(output, /import\.cjs was not initialized, initializing the in-memory entry/);
        assert.match(output, /writing cache for .*import\.cjs.*success/);
        return true;
      }
    });

  spawnSyncAndAssert(
    process.execPath,
    [cjs],
    {
      env: {
        ...process.env,
        NODE_DEBUG_NATIVE: 'COMPILE_CACHE',
        NODE_COMPILE_CACHE: dir
      },
      cwd: tmpdir.path
    },
    {
      trim: true,
      stdout: 'hello world',
      stderr(output) {
        assert.match(output, /cache for .*mod\.js was accepted, keeping the in-memory entry/);
        assert.match(output, /.*skip .*mod\.js because cache was the same/);
        assert.match(output, /cache for .*import\.cjs was accepted, keeping the in-memory entry/);
        assert.match(output, /.*skip .*import\.cjs because cache was the same/);
        return true;
      }
    });

  spawnSyncAndAssert(
    process.execPath,
    [mjs],
    {
      env: {
        ...process.env,
        NODE_DEBUG_NATIVE: 'COMPILE_CACHE',
        NODE_COMPILE_CACHE: dir
      },
      cwd: tmpdir.path
    },
    {
      trim: true,
      stdout: 'hello world',
      stderr(output) {
        assert.match(output, /cache for .*mod\.js was accepted, keeping the in-memory entry/);
        assert.match(output, /.*skip .*mod\.js because cache was the same/);
        assert.match(output, /reading cache .* for ESM .*import\.mjs/);
        assert.match(output, /import\.mjs was not initialized, initializing the in-memory entry/);
        assert.match(output, /writing cache for .*import\.mjs.*success/);
        return true;
      }
    });

  spawnSyncAndAssert(
    process.execPath,
    [mjs],
    {
      env: {
        ...process.env,
        NODE_DEBUG_NATIVE: 'COMPILE_CACHE',
        NODE_COMPILE_CACHE: dir
      },
      cwd: tmpdir.path
    },
    {
      trim: true,
      stdout: 'hello world',
      stderr(output) {
        assert.match(output, /cache for .*mod\.js was accepted, keeping the in-memory entry/);
        assert.match(output, /.*skip .*mod\.js because cache was the same/);
        assert.match(output, /cache for .*import\.mjs was accepted, keeping the in-memory entry/);
        assert.match(output, /.*skip .*import\.mjs because cache was the same/);
        return true;
      }
    });
}
