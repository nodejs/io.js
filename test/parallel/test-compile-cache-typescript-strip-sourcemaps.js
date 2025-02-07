'use strict';

// This tests NODE_COMPILE_CACHE can be used for type stripping and ignores
// --enable-source-maps as there's no difference in the code generated.

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

// Reloading with source maps should hit the cache generated without source maps, because for
// type stripping, only sourceURL is added regardless of whether source map is enabled.
spawnSyncAndAssert(
  process.execPath,
  ['--enable-source-maps', script],
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
      assert.match(output, /retrieving transpile cache for StrippedTypeScript .*test-typescript\.ts.*success/);
      assert.match(output, /reading cache from .* for CommonJS .*test-typescript\.ts.*success/);
      assert.match(output, /skip persisting StrippedTypeScript .*test-typescript\.ts because cache was the same/);
      assert.match(output, /V8 code cache for CommonJS .*test-typescript\.ts was accepted, keeping the in-memory entry/);
      assert.match(output, /skip persisting CommonJS .*test-typescript\.ts because cache was the same/);
      return true;
    }
  });
