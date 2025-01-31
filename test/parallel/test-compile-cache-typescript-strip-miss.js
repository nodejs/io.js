'use strict';

// This tests NODE_COMPILE_CACHE can handle cache invalidation
// between strip-only TypeScript and transformed TypeScript.

require('../common');
const { spawnSyncAndAssert } = require('../common/child_process');
const assert = require('assert');
const tmpdir = require('../common/tmpdir');
const fixtures = require('../common/fixtures');

tmpdir.refresh();
const dir = tmpdir.resolve('.compile_cache_dir');
const script = fixtures.path('typescript', 'ts', 'test-typescript.ts');

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
      assert.match(output, /saving transpilation cache for StrippedTypeScript .*test-typescript\.ts/);
      assert.match(output, /writing cache for StrippedTypeScript .*test-typescript\.ts.*success/);
      assert.match(output, /writing cache for CommonJS .*test-typescript\.ts.*success/);
      return true;
    }
  });

// Reloading with transform should miss the cache generated without transform.
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
      // Both the transpile cache and the code cache should be missed.
      assert.match(output, /no transpile cache for TransformedTypeScriptWithSourceMaps .*test-typescript\.ts/);
      assert.match(output, /reading cache from .* for CommonJS .*test-typescript\.ts.*mismatch/);
      // New cache with source map should be generated.
      assert.match(output, /writing cache for TransformedTypeScriptWithSourceMaps .*test-typescript\.ts.*success/);
      assert.match(output, /writing cache for CommonJS .*test-typescript\.ts.*success/);
      return true;
    }
  });

// Reloading with transform should hit the cache generated with transform.
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
      assert.match(output, /retrieving transpile cache for TransformedTypeScriptWithSourceMaps .*test-typescript\.ts.*success/);
      assert.match(output, /reading cache from .* for CommonJS .*test-typescript\.ts.*success/);
      assert.match(output, /skip persisting TransformedTypeScriptWithSourceMaps .*test-typescript\.ts because cache was the same/);
      assert.match(output, /V8 code cache for CommonJS .*test-typescript\.ts was accepted, keeping the in-memory entry/);
      assert.match(output, /skip persisting CommonJS .*test-typescript\.ts because cache was the same/);
      return true;
    }
  });

// Reloading without transform should hit the co-existing transpile cache generated without transform,
// but miss the code cache generated with transform.
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
      assert.match(output, /retrieving transpile cache for StrippedTypeScript .*test-typescript\.ts.*success/);
      assert.match(output, /reading cache from .* for CommonJS .*test-typescript\.ts.*mismatch/);
      assert.match(output, /skip persisting StrippedTypeScript .*test-typescript\.ts because cache was the same/);
      assert.match(output, /writing cache for CommonJS .*test-typescript\.ts.*success/);
      return true;
    }
  });
