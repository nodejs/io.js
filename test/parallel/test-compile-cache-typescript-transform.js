'use strict';

// This tests NODE_COMPILE_CACHE works with --experimental-transform-types.

require('../common');
const { spawnSyncAndAssert } = require('../common/child_process');
const assert = require('assert');
const tmpdir = require('../common/tmpdir');
const fixtures = require('../common/fixtures');


tmpdir.refresh();
const dir = tmpdir.resolve('.compile_cache_dir');
const script = fixtures.path('typescript', 'ts', 'transformation', 'test-enum.ts');

// Check --experimental-transform-types which enables source maps by default.
spawnSyncAndAssert(
  process.execPath,
  ['--experimental-transform-types', script],
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
      assert.match(output, /saving transpilation cache for TransformedTypeScriptWithSourceMaps .*test-enum\.ts/);
      assert.match(output, /writing cache for TransformedTypeScriptWithSourceMaps .*test-enum\.ts.*success/);
      assert.match(output, /writing cache for CommonJS .*test-enum\.ts.*success/);
      return true;
    }
  });

spawnSyncAndAssert(
  process.execPath,
  ['--experimental-transform-types', script],
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
      assert.match(output, /retrieving transpile cache for TransformedTypeScriptWithSourceMaps .*test-enum\.ts.*success/);
      assert.match(output, /reading cache from .* for CommonJS .*test-enum\.ts.*success/);
      assert.match(output, /skip persisting TransformedTypeScriptWithSourceMaps .*test-enum\.ts because cache was the same/);
      assert.match(output, /V8 code cache for CommonJS .*test-enum\.ts was accepted, keeping the in-memory entry/);
      assert.match(output, /skip persisting CommonJS .*test-enum\.ts because cache was the same/);
      return true;
    }
  });

// Reloading without source maps should miss the cache generated with source maps.
spawnSyncAndAssert(
  process.execPath,
  ['--experimental-transform-types', '--no-enable-source-maps', script],
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
      // Both the transpile cache and the code cache should be missed.
      assert.match(output, /no transpile cache for TransformedTypeScript .*test-enum\.ts/);
      assert.match(output, /reading cache from .* for CommonJS .*test-enum\.ts.*mismatch/);
      // New cache without source map should be generated.
      assert.match(output, /writing cache for TransformedTypeScript .*test-enum\.ts.*success/);
      assert.match(output, /writing cache for CommonJS .*test-enum\.ts.*success/);
      return true;
    }
  });

// Reloading without source maps again should hit the cache generated without source maps.
spawnSyncAndAssert(
  process.execPath,
  ['--experimental-transform-types', '--no-enable-source-maps', script],
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
      assert.match(output, /retrieving transpile cache for TransformedTypeScript .*test-enum\.ts.*success/);
      assert.match(output, /reading cache from .* for CommonJS .*test-enum\.ts.*success/);
      assert.match(output, /skip persisting TransformedTypeScript .*test-enum\.ts because cache was the same/);
      assert.match(output, /V8 code cache for CommonJS .*test-enum\.ts was accepted, keeping the in-memory entry/);
      assert.match(output, /skip persisting CommonJS .*test-enum\.ts because cache was the same/);
      return true;
    }
  });

// Reloading with source maps again should hit the co-existing transpile cache with source
// maps, but miss the code cache generated without source maps.
spawnSyncAndAssert(
  process.execPath,
  ['--experimental-transform-types', script],
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
      assert.match(output, /retrieving transpile cache for TransformedTypeScriptWithSourceMaps .*test-enum\.ts.*success/);
      assert.match(output, /reading cache from .* for CommonJS .*test-enum\.ts.*mismatch/);
      assert.match(output, /skip persisting TransformedTypeScriptWithSourceMaps .*test-enum\.ts because cache was the same/);
      assert.match(output, /writing cache for CommonJS .*test-enum\.ts.*success/);
      return true;
    }
  });
