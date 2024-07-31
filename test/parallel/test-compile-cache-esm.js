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
  const script = fixtures.path('es-modules', 'message.mjs');

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
        assert.match(output, /message\.mjs was not initialized, initializing the in-memory entry/);
        assert.match(output, /writing cache for .*message\.mjs.*success/);
        return true;
      }
    });

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
        assert.match(output, /cache for .*message\.mjs was accepted, keeping the in-memory entry/);
        assert.match(output, /.*skip .*message\.mjs because cache was the same/);
        return true;
      }
    });
}
