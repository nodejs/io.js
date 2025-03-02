import { skip, spawnPromisified } from '../common/index.mjs';
import * as fixtures from '../common/fixtures.mjs';
import { match, strictEqual } from 'node:assert';
import { test } from 'node:test';

if (!process.config.variables.node_use_amaro) skip('Requires Amaro');

test('execute a TypeScript file with transformation enabled', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-transform-types',
    '--no-warnings',
    fixtures.path('typescript/ts/transformation/test-enum.ts'),
  ]);

  strictEqual(result.stderr, '');
  match(result.stdout, /Hello, TypeScript!/);
  strictEqual(result.code, 0);
});

test('reconstruct error of a TypeScript file with transformation enabled and sourcemaps', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-transform-types',
    '--no-warnings',
    fixtures.path('typescript/ts/transformation/test-enum-stacktrace.ts'),
  ]);

  match(result.stderr, /test-enum-stacktrace\.ts:4:7/);
  strictEqual(result.stdout, '');
  strictEqual(result.code, 1);
});

test('reconstruct error of a TypeScript file with transformation enabled without sourcemaps', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-transform-types',
    '--no-enable-source-maps',
    '--no-warnings',
    fixtures.path('typescript/ts/transformation/test-enum-stacktrace.ts'),
  ]);
  match(result.stderr, /test-enum-stacktrace\.ts:5:7/);
  strictEqual(result.stdout, '');
  strictEqual(result.code, 1);
});

test('should not elide unused imports', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-transform-types',
    '--no-warnings',
    fixtures.path('typescript/ts/transformation/test-unused-import.ts'),
  ]);
  match(result.stderr, /ERR_UNSUPPORTED_DIR_IMPORT/);
  strictEqual(result.stdout, '');
  strictEqual(result.code, 1);
});

test('execute a TypeScript file with namespace', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-transform-types',
    '--no-warnings',
    fixtures.path('typescript/ts/transformation/test-namespace.ts'),
  ]);

  strictEqual(result.stderr, '');
  match(result.stdout, /Hello, TypeScript!/);
  strictEqual(result.code, 0);
});

// Decorators are currently ignored by transpilation
// and will be unusable until V8 adds support for them.
test('execute a TypeScript file with decorator', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-transform-types',
    '--no-warnings',
    fixtures.path('typescript/ts/transformation/test-decorator.ts'),
  ]);

  strictEqual(result.stdout, '');
  match(result.stderr, /Invalid or unexpected token/);
  strictEqual(result.code, 1);
});

test('execute a TypeScript file with legacy-module', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-transform-types',
    '--no-warnings',
    fixtures.path('typescript/ts/transformation/test-legacy-module.ts'),
  ]);

  match(result.stderr, /ERR_UNSUPPORTED_TYPESCRIPT_SYNTAX/);
  match(result.stderr, /`module` keyword is not supported\. Use `namespace` instead/);
  strictEqual(result.stdout, '');
  strictEqual(result.code, 1);
});

test('execute a TypeScript file with modern typescript syntax', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-transform-types',
    '--no-warnings',
    fixtures.path('typescript/ts/transformation/test-modern-typescript.ts'),
  ]);

  strictEqual(result.stderr, '');
  match(result.stdout, /Hello, TypeScript!/);
  strictEqual(result.code, 0);
});

test('execute a transpiled JavaScript file', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--enable-source-maps',
    '--no-warnings',
    fixtures.path('typescript/ts/transformation/test-transformed-typescript.js'),
  ]);

  match(result.stderr, /Stacktrace at line 28/);
  match(result.stderr, /test-failing-arm64\.js:28:7/);
  strictEqual(result.stdout, '');
  strictEqual(result.code, 1);
});

test('execute TypeScript file with import = require', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-transform-types',
    '--no-warnings',
    fixtures.path('typescript/cts/test-import-require.cts'),
  ]);

  strictEqual(result.stderr, '');
  match(result.stdout, /Hello, TypeScript!/);
  strictEqual(result.code, 0);
});
