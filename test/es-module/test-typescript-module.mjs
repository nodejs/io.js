import { spawnPromisified } from '../common/index.mjs';
import * as fixtures from '../common/fixtures.mjs';
import { match, strictEqual } from 'node:assert';
import { test } from 'node:test';

test('expect failure of a .mts file with CommonJS syntax', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-strip-types',
    fixtures.path('typescript/mts/test-mts-but-commonjs-syntax.mts'),
  ]);

  strictEqual(result.stdout, '');
  match(result.stderr, /require is not defined in ES module scope, you can use import instead/);
  strictEqual(result.code, 1);
});

test('execute an .mts file importing an .mts file', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-strip-types',
    fixtures.path('typescript/mts/test-import-module.mts'),
  ]);

  match(result.stderr, /Type Stripping is an experimental feature and might change at any time/);
  match(result.stdout, /Hello, TypeScript!/);
  strictEqual(result.code, 0);
});

test('execute an .mts file importing a .ts file', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-strip-types',
    '--experimental-default-type=module', // this should fail
    '--no-warnings',
    fixtures.path('typescript/mts/test-import-ts-file.mts'),
  ]);

  strictEqual(result.stderr, '');
  match(result.stdout, /Hello, TypeScript!/);
  strictEqual(result.code, 0);
});

test('execute an .mts file importing a .cts file', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-strip-types',
    '--no-warnings',
    '--no-warnings',
    fixtures.path('typescript/mts/test-import-commonjs.mts'),
  ]);

  strictEqual(result.stderr, '');
  match(result.stdout, /Hello, TypeScript!/);
  strictEqual(result.code, 0);
});

test('execute an .mts file with wrong default module', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-strip-types',
    '--experimental-default-type=commonjs',
    fixtures.path('typescript/mts/test-import-module.mts'),
  ]);

  strictEqual(result.stdout, '');
  match(result.stderr, /Error \[ERR_REQUIRE_ESM\]: require\(\) of ES Module/);
  strictEqual(result.code, 1);
});

test('execute an .mts file from node_modules', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-strip-types',
    fixtures.path('typescript/mts/test-mts-node_modules.mts'),
  ]);

  match(result.stderr, /ERR_UNSUPPORTED_NODE_MODULES_TYPE_STRIPPING/);
  strictEqual(result.stdout, '');
  strictEqual(result.code, 1);
});

test('execute a .cts file from node_modules', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-strip-types',
    fixtures.path('typescript/mts/test-cts-node_modules.mts'),
  ]);

  match(result.stderr, /ERR_UNSUPPORTED_NODE_MODULES_TYPE_STRIPPING/);
  strictEqual(result.stdout, '');
  strictEqual(result.code, 1);
});

test('execute a .ts file from node_modules', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-strip-types',
    fixtures.path('typescript/mts/test-ts-node_modules.mts'),
  ]);

  match(result.stderr, /ERR_UNSUPPORTED_NODE_MODULES_TYPE_STRIPPING/);
  strictEqual(result.stdout, '');
  strictEqual(result.code, 1);
});
