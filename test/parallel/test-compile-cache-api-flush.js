'use strict';

// This tests module.flushCompileCache() works as expected.

require('../common');
const { spawnSyncAndAssert } = require('../common/child_process');
const assert = require('assert');
const tmpdir = require('../common/tmpdir');
const fixtures = require('../common/fixtures');

{
  // Test that it works with non-existent directory.
  tmpdir.refresh();
  const cacheDir = tmpdir.resolve('compile_cache');
  spawnSyncAndAssert(
    process.execPath,
    [fixtures.path('compile-cache-flush.js')],
    {
      env: {
        ...process.env,
        NODE_DEBUG_NATIVE: 'COMPILE_CACHE',
        NODE_COMPILE_CACHE: cacheDir,
      },
      cwd: tmpdir.path
    },
    {
      stdout(output) {
        // This contains output from the nested spawnings of compile-cache-flush.js.
        assert.match(output, /child1.* cache for .*compile-cache-flush\.js was accepted, keeping the in-memory entry/);
        assert.match(output, /child2.* cache for .*compile-cache-flush\.js was accepted, keeping the in-memory entry/);
        return true;
      },
      stderr(output) {
        // This contains output from the top-level spawning of compile-cache-flush.js.
        assert.match(output, /reading cache from .*compile_cache.* for CommonJS .*compile-cache-flush\.js/);
        assert.match(output, /compile-cache-flush\.js was not initialized, initializing the in-memory entry/);

        const writeRE = /writing cache for .*compile-cache-flush\.js.*success/;
        const flushRE = /module\.flushCompileCache\(\) finished/;
        assert.match(output, writeRE);
        assert.match(output, flushRE);
        // The cache writing should happen before flushing finishes i.e. it's not delayed until process shutdown.
        assert(output.match(writeRE).index < output.match(flushRE).index);
        return true;
      }
    });
}
