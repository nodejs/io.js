'use strict';

// This tests NODE_COMPILE_CACHE works for ESM with types.

require('../common');
const { spawnSyncAndAssert } = require('../common/child_process');
const assert = require('assert');
const tmpdir = require('../common/tmpdir');
const fixtures = require('../common/fixtures');

// Check cache for .ts files that would be run as ESM.
{
  tmpdir.refresh();
  const dir = tmpdir.resolve('.compile_cache_dir');
  const script = fixtures.path('typescript', 'ts', 'test-module-typescript.ts');

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
        assert.match(output, /saving transpilation cache for StrippedTypeScript .*test-module-typescript\.ts/);
        assert.match(output, /writing cache for StrippedTypeScript .*test-module-typescript\.ts.*success/);
        assert.match(output, /writing cache for ESM .*test-module-typescript\.ts.*success/);
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
        assert.match(output, /retrieving transpile cache for StrippedTypeScript .*test-module-typescript\.ts.*success/);
        assert.match(output, /reading cache from .* for ESM .*test-module-typescript\.ts.*success/);
        assert.match(output, /skip persisting StrippedTypeScript .*test-module-typescript\.ts because cache was the same/);
        assert.match(output, /V8 code cache for ESM .*test-module-typescript\.ts was accepted, keeping the in-memory entry/);
        assert.match(output, /skip persisting ESM .*test-module-typescript\.ts because cache was the same/);
        return true;
      }
    });
}

// Check cache for .mts files that import .mts files.
{
  tmpdir.refresh();
  const dir = tmpdir.resolve('.compile_cache_dir');
  const script = fixtures.path('typescript', 'mts', 'test-import-module.mts');

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
        assert.match(output, /writing cache for StrippedTypeScript .*test-import-module\.mts.*success/);
        assert.match(output, /writing cache for StrippedTypeScript .*test-mts-export-foo\.mts.*success/);
        assert.match(output, /writing cache for ESM .*test-import-module\.mts.*success/);
        assert.match(output, /writing cache for ESM .*test-mts-export-foo\.mts.*success/);
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
        assert.match(output, /retrieving transpile cache for StrippedTypeScript .*test-import-module\.mts.*success/);
        assert.match(output, /skip persisting StrippedTypeScript .*test-import-module\.mts because cache was the same/);
        assert.match(output, /retrieving transpile cache for StrippedTypeScript .*test-mts-export-foo\.mts.*success/);
        assert.match(output, /skip persisting StrippedTypeScript .*test-mts-export-foo\.mts because cache was the same/);

        assert.match(output, /V8 code cache for ESM .*test-import-module\.mts was accepted, keeping the in-memory entry/);
        assert.match(output, /skip persisting ESM .*test-import-module\.mts because cache was the same/);
        assert.match(output, /V8 code cache for ESM .*test-mts-export-foo\.mts was accepted, keeping the in-memory entry/);
        assert.match(output, /skip persisting ESM .*test-mts-export-foo\.mts because cache was the same/);
        return true;
      }
    });
}


// Check cache for .mts files that import .cts files.
{
  tmpdir.refresh();
  const dir = tmpdir.resolve('.compile_cache_dir');
  const script = fixtures.path('typescript', 'mts', 'test-import-commonjs.mts');

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
        assert.match(output, /writing cache for StrippedTypeScript .*test-import-commonjs\.mts.*success/);
        assert.match(output, /writing cache for StrippedTypeScript .*test-cts-export-foo\.cts.*success/);
        assert.match(output, /writing cache for ESM .*test-import-commonjs\.mts.*success/);
        assert.match(output, /writing cache for CommonJS .*test-cts-export-foo\.cts.*success/);
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
        assert.match(output, /retrieving transpile cache for StrippedTypeScript .*test-import-commonjs\.mts.*success/);
        assert.match(output, /skip persisting StrippedTypeScript .*test-import-commonjs\.mts because cache was the same/);
        assert.match(output, /retrieving transpile cache for StrippedTypeScript .*test-cts-export-foo\.cts.*success/);
        assert.match(output, /skip persisting StrippedTypeScript .*test-cts-export-foo\.cts because cache was the same/);

        assert.match(output, /V8 code cache for ESM .*test-import-commonjs\.mts was accepted, keeping the in-memory entry/);
        assert.match(output, /skip persisting ESM .*test-import-commonjs\.mts because cache was the same/);
        assert.match(output, /V8 code cache for CommonJS .*test-cts-export-foo\.cts was accepted, keeping the in-memory entry/);
        assert.match(output, /skip persisting CommonJS .*test-cts-export-foo\.cts because cache was the same/);
        return true;
      }
    });
}
