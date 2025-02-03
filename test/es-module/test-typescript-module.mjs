import { skip, spawnPromisified } from '../common/index.mjs';
import * as fixtures from '../common/fixtures.mjs';
import { match, strictEqual } from 'node:assert';
import { test } from 'node:test';

if (!process.config.variables.node_use_amaro) skip('Requires Amaro');

test('expect failure of a .mts file with CommonJS syntax', async () => {
  const result = await spawnPromisified(process.execPath, [
    fixtures.path('typescript/mts/test-mts-but-commonjs-syntax.mts'),
  ]);

  strictEqual(result.stdout, '');
  match(result.stderr, /require is not defined in ES module scope, you can use import instead/);
  strictEqual(result.code, 1);
});

test('execute an .mts file importing an .mts file', async () => {
  const result = await spawnPromisified(process.execPath, [
    fixtures.path('typescript/mts/test-import-module.mts'),
  ]);

  match(result.stderr, /Type Stripping is an experimental feature and might change at any time/);
  match(result.stdout, /Hello, TypeScript!/);
  strictEqual(result.code, 0);
});

test('execute an .mts file importing a .ts file', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--no-warnings',
    fixtures.path('typescript/mts/test-import-ts-file.mts'),
  ]);

  strictEqual(result.stderr, '');
  match(result.stdout, /Hello, TypeScript!/);
  strictEqual(result.code, 0);
});

test('execute an .mts file importing a .cts file', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--no-warnings',
    fixtures.path('typescript/mts/test-import-commonjs.mts'),
  ]);

  strictEqual(result.stderr, '');
  match(result.stdout, /Hello, TypeScript!/);
  strictEqual(result.code, 0);
});

test('execute an .mts file from node_modules', async () => {
  const result = await spawnPromisified(process.execPath, [
    fixtures.path('typescript/mts/test-mts-node_modules.mts'),
  ]);

  match(result.stderr, /ERR_UNSUPPORTED_NODE_MODULES_TYPE_STRIPPING/);
  strictEqual(result.stdout, '');
  strictEqual(result.code, 1);
});

test('execute a .cts file from node_modules', async () => {
  const result = await spawnPromisified(process.execPath, [
    fixtures.path('typescript/mts/test-cts-node_modules.mts'),
  ]);

  match(result.stderr, /ERR_UNSUPPORTED_NODE_MODULES_TYPE_STRIPPING/);
  strictEqual(result.stdout, '');
  strictEqual(result.code, 1);
});

test('execute a .ts file from node_modules', async () => {
  const result = await spawnPromisified(process.execPath, [
    fixtures.path('typescript/mts/test-ts-node_modules.mts'),
  ]);

  match(result.stderr, /ERR_UNSUPPORTED_NODE_MODULES_TYPE_STRIPPING/);
  strictEqual(result.stdout, '');
  strictEqual(result.code, 1);
});

test('execute an empty .ts file', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--no-warnings',
    fixtures.path('typescript/ts/test-empty-file.ts'),
  ]);

  strictEqual(result.stderr, '');
  strictEqual(result.stdout, '');
  strictEqual(result.code, 0);
});

test('execute .ts file importing a module', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--no-warnings',
    fixtures.path('typescript/ts/test-import-fs.ts'),
  ]);

  strictEqual(result.stderr, '');
  strictEqual(result.stdout, 'Hello, TypeScript!\n');
  strictEqual(result.code, 0);
});
