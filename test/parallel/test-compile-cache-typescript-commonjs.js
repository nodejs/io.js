'use strict';

// This tests NODE_COMPILE_CACHE works for CommonJS with types.

require('../common');
const { spawnSyncAndAssert } = require('../common/child_process');
const assert = require('assert');
const tmpdir = require('../common/tmpdir');
const fixtures = require('../common/fixtures');

// Check cache for .ts files that would be run as CommonJS.
{
  tmpdir.refresh();
  const dir = tmpdir.resolve('.compile_cache_dir');
  const script = fixtures.path('typescript', 'ts', 'test-commonjs-parsing.ts');

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
        assert.match(output, /saving transpilation cache for StrippedTypeScript .*test-commonjs-parsing\.ts/);
        assert.match(output, /writing cache for StrippedTypeScript .*test-commonjs-parsing\.ts.*success/);
        assert.match(output, /writing cache for CommonJS .*test-commonjs-parsing\.ts.*success/);
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
        assert.match(output, /retrieving transpile cache for StrippedTypeScript .*test-commonjs-parsing\.ts.*success/);
        assert.match(output, /reading cache from .* for CommonJS .*test-commonjs-parsing\.ts.*success/);
        assert.match(output, /skip persisting StrippedTypeScript .*test-commonjs-parsing\.ts because cache was the same/);
        assert.match(output, /V8 code cache for CommonJS .*test-commonjs-parsing\.ts was accepted, keeping the in-memory entry/);
        assert.match(output, /skip persisting CommonJS .*test-commonjs-parsing\.ts because cache was the same/);
        return true;
      }
    });
}

// Check cache for .cts files that require .cts files.
{
  tmpdir.refresh();
  const dir = tmpdir.resolve('.compile_cache_dir');
  const script = fixtures.path('typescript', 'cts', 'test-require-commonjs.cts');

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
        assert.match(output, /writing cache for StrippedTypeScript .*test-require-commonjs\.cts.*success/);
        assert.match(output, /writing cache for StrippedTypeScript .*test-cts-export-foo\.cts.*success/);
        assert.match(output, /writing cache for CommonJS .*test-require-commonjs\.cts.*success/);
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
        assert.match(output, /retrieving transpile cache for StrippedTypeScript .*test-require-commonjs\.cts.*success/);
        assert.match(output, /skip persisting StrippedTypeScript .*test-require-commonjs\.cts because cache was the same/);
        assert.match(output, /retrieving transpile cache for StrippedTypeScript .*test-cts-export-foo\.cts.*success/);
        assert.match(output, /skip persisting StrippedTypeScript .*test-cts-export-foo\.cts because cache was the same/);

        assert.match(output, /V8 code cache for CommonJS .*test-require-commonjs\.cts was accepted, keeping the in-memory entry/);
        assert.match(output, /skip persisting CommonJS .*test-require-commonjs\.cts because cache was the same/);
        assert.match(output, /V8 code cache for CommonJS .*test-cts-export-foo\.cts was accepted, keeping the in-memory entry/);
        assert.match(output, /skip persisting CommonJS .*test-cts-export-foo\.cts because cache was the same/);
        return true;
      }
    });
}

// Check cache for .cts files that require .mts files.
{
  tmpdir.refresh();
  const dir = tmpdir.resolve('.compile_cache_dir');
  const script = fixtures.path('typescript', 'cts', 'test-require-mts-module.cts');

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
        assert.match(output, /writing cache for StrippedTypeScript .*test-require-mts-module\.cts.*success/);
        assert.match(output, /writing cache for StrippedTypeScript .*test-mts-export-foo\.mts.*success/);
        assert.match(output, /writing cache for CommonJS .*test-require-mts-module\.cts.*success/);
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
        assert.match(output, /retrieving transpile cache for StrippedTypeScript .*test-require-mts-module\.cts.*success/);
        assert.match(output, /skip persisting StrippedTypeScript .*test-require-mts-module\.cts because cache was the same/);
        assert.match(output, /retrieving transpile cache for StrippedTypeScript .*test-mts-export-foo\.mts.*success/);
        assert.match(output, /skip persisting StrippedTypeScript .*test-mts-export-foo\.mts because cache was the same/);

        assert.match(output, /V8 code cache for CommonJS .*test-require-mts-module\.cts was accepted, keeping the in-memory entry/);
        assert.match(output, /skip persisting CommonJS .*test-require-mts-module\.cts because cache was the same/);
        assert.match(output, /V8 code cache for ESM .*test-mts-export-foo\.mts was accepted, keeping the in-memory entry/);
        assert.match(output, /skip persisting ESM .*test-mts-export-foo\.mts because cache was the same/);
        return true;
      }
    });
}
