import { spawnPromisified } from '../common/index.mjs';
import * as fixtures from '../common/fixtures.mjs';
import { match, strictEqual } from 'node:assert';
import { test } from 'node:test';

test('require a .ts file with explicit extension succeeds', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-strip-types',
    '--eval',
    'require("./test-typescript.ts")',
    '--no-warnings',
  ], {
    cwd: fixtures.path('typescript/ts'),
  });

  strictEqual(result.stderr, '');
  strictEqual(result.stdout, 'Hello, TypeScript!\n');
  strictEqual(result.code, 0);
});

test('eval require a .ts file with implicit extension fails', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-strip-types',
    '--eval',
    'require("./test-typescript")',
    '--no-warnings',
  ], {
    cwd: fixtures.path('typescript/ts'),
  });

  strictEqual(result.stdout, '');
  match(result.stderr, /Error: Cannot find module/);
  strictEqual(result.code, 1);
});

test('eval require a .cts file with implicit extension fails', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-strip-types',
    '--eval',
    'require("./test-cts-typescript")',
    '--no-warnings',
  ], {
    cwd: fixtures.path('typescript/ts'),
  });

  strictEqual(result.stdout, '');
  match(result.stderr, /Error: Cannot find module/);
  strictEqual(result.code, 1);
});

test('require a .ts file with implicit extension fails', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-strip-types',
    '--no-warnings',
    fixtures.path('typescript/cts/test-extensionless-require.ts'),
  ]);

  strictEqual(result.stdout, '');
  match(result.stderr, /Error: Cannot find module/);
  strictEqual(result.code, 1);
});

test('expect failure of an .mts file with CommonJS syntax', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-strip-types',
    fixtures.path('typescript/cts/test-cts-but-module-syntax.cts'),
  ]);

  strictEqual(result.stdout, '');
  match(result.stderr, /To load an ES module, set "type": "module" in the package\.json or use the \.mjs extension\./);
  strictEqual(result.code, 1);
});

test('execute a .cts file importing a .cts file', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-strip-types',
    '--no-warnings',
    fixtures.path('typescript/cts/test-require-commonjs.cts'),
  ]);

  strictEqual(result.stderr, '');
  match(result.stdout, /Hello, TypeScript!/);
  strictEqual(result.code, 0);
});

test('execute a .cts file importing a .ts file export', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-strip-types',
    '--no-warnings',
    fixtures.path('typescript/cts/test-require-ts-file.cts'),
  ]);

  strictEqual(result.stderr, '');
  match(result.stdout, /Hello, TypeScript!/);
  strictEqual(result.code, 0);
});

test('execute a .cts file importing a .mts file export', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-strip-types',
    fixtures.path('typescript/cts/test-require-mts-module.cts'),
  ]);

  strictEqual(result.stdout, '');
  match(result.stderr, /Error \[ERR_REQUIRE_ESM\]: require\(\) of ES Module/);
  strictEqual(result.code, 1);
});

test('execute a .cts file importing a .mts file export', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-strip-types',
    '--experimental-require-module',
    fixtures.path('typescript/cts/test-require-mts-module.cts'),
  ]);

  match(result.stderr, /Support for loading ES Module in require\(\) is an experimental feature and might change at any time/);
  match(result.stdout, /Hello, TypeScript!/);
  strictEqual(result.code, 0);
});

test('expect failure of a .cts file with default type module', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-strip-types',
    '--experimental-default-type=module', // Keeps working with commonjs
    '--no-warnings',
    fixtures.path('typescript/cts/test-require-commonjs.cts'),
  ]);

  strictEqual(result.stderr, '');
  match(result.stdout, /Hello, TypeScript!/);
  strictEqual(result.code, 0);
});

test('expect failure of a .cts file in node_modules', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-strip-types',
    fixtures.path('typescript/cts/test-cts-node_modules.cts'),
  ]);

  strictEqual(result.stdout, '');
  match(result.stderr, /ERR_UNSUPPORTED_NODE_MODULES_TYPE_STRIPPING/);
  strictEqual(result.code, 1);
});

test('expect failure of a .ts file in node_modules', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-strip-types',
    fixtures.path('typescript/cts/test-ts-node_modules.cts'),
  ]);

  strictEqual(result.stdout, '');
  match(result.stderr, /ERR_UNSUPPORTED_NODE_MODULES_TYPE_STRIPPING/);
  strictEqual(result.code, 1);
});

test('expect failure of a .cts requiring esm without default type module', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-strip-types',
    fixtures.path('typescript/cts/test-mts-node_modules.cts'),
  ]);

  strictEqual(result.stdout, '');
  match(result.stderr, /ERR_REQUIRE_ESM/);
  strictEqual(result.code, 1);
});

test('expect failure of a .cts file requiring esm in node_modules', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-strip-types',
    '--experimental-require-module',
    fixtures.path('typescript/cts/test-mts-node_modules.cts'),
  ]);

  strictEqual(result.stdout, '');
  match(result.stderr, /ERR_UNSUPPORTED_NODE_MODULES_TYPE_STRIPPING/);
  strictEqual(result.code, 1);
});
