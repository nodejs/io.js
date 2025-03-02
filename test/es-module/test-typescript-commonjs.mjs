import { skip, spawnPromisified } from '../common/index.mjs';
import * as fixtures from '../common/fixtures.mjs';
import assert, { match, strictEqual } from 'node:assert';
import { test } from 'node:test';

if (!process.config.variables.node_use_amaro) skip('Requires Amaro');

test('require a .ts file with explicit extension succeeds', async () => {
  const result = await spawnPromisified(process.execPath, [
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
    '--no-warnings',
    fixtures.path('typescript/cts/test-extensionless-require.ts'),
  ]);

  strictEqual(result.stdout, '');
  match(result.stderr, /Error: Cannot find module/);
  strictEqual(result.code, 1);
});

test('expect failure of an .mts file with CommonJS syntax', async () => {
  const testFilePath = fixtures.path(
    'typescript/cts/test-cts-but-module-syntax.cts'
  );
  const result = await spawnPromisified(process.execPath, [testFilePath]);

  assert.strictEqual(result.stdout, '');

  const expectedWarning = `Failed to load the ES module: ${testFilePath}. Make sure to set "type": "module" in the nearest package.json file or use the .mjs extension.`;

  // First assertion: check that stderr contains a generic pattern for the ES module load failure.
  // This ensures that at a minimum the error message structure is as expected.
  assert.match(
    result.stderr,
    /Failed to load the ES module:.*test-cts-but-module-syntax\.cts/
  );

  // Second assertion: verify that stderr includes the complete expected warning message.
  // We use a try/catch here to improve the error output, providing more context if the assertion fails.
  // Having two assertions ensures that:
  //   1. We first confirm the error message follows the expected general format.
  //   2. We then check that the specific warning (with file path and message details) is present.
  try {
    assert.ok(
      result.stderr.includes(expectedWarning),
      `Expected stderr to include: ${expectedWarning}`
    );
  } catch (e) {
    if (e?.code === 'ERR_ASSERTION') {
      e.expected = expectedWarning;
      e.actual = result.stderr;
      e.operator = 'includes';
    }
    throw e;
  }


  assert.strictEqual(result.code, 1);
});

test('execute a .cts file importing a .cts file', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--no-warnings',
    fixtures.path('typescript/cts/test-require-commonjs.cts'),
  ]);

  strictEqual(result.stderr, '');
  match(result.stdout, /Hello, TypeScript!/);
  strictEqual(result.code, 0);
});

test('execute a .cts file importing a .ts file export', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--no-warnings',
    fixtures.path('typescript/cts/test-require-ts-file.cts'),
  ]);

  strictEqual(result.stderr, '');
  match(result.stdout, /Hello, TypeScript!/);
  strictEqual(result.code, 0);
});

test('execute a .cts file importing a .mts file export', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--no-experimental-require-module',
    fixtures.path('typescript/cts/test-require-mts-module.cts'),
  ]);

  strictEqual(result.stdout, '');
  match(result.stderr, /Error \[ERR_REQUIRE_ESM\]: require\(\) of ES Module/);
  strictEqual(result.code, 1);
});

test('execute a .cts file importing a .mts file export', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-require-module',
    fixtures.path('typescript/cts/test-require-mts-module.cts'),
  ]);

  match(result.stdout, /Hello, TypeScript!/);
  strictEqual(result.code, 0);
});

test('expect failure of a .cts file in node_modules', async () => {
  const result = await spawnPromisified(process.execPath, [
    fixtures.path('typescript/cts/test-cts-node_modules.cts'),
  ]);

  strictEqual(result.stdout, '');
  match(result.stderr, /ERR_UNSUPPORTED_NODE_MODULES_TYPE_STRIPPING/);
  strictEqual(result.code, 1);
});

test('expect failure of a .ts file in node_modules', async () => {
  const result = await spawnPromisified(process.execPath, [
    fixtures.path('typescript/cts/test-ts-node_modules.cts'),
  ]);

  strictEqual(result.stdout, '');
  match(result.stderr, /ERR_UNSUPPORTED_NODE_MODULES_TYPE_STRIPPING/);
  strictEqual(result.code, 1);
});

test('expect failure of a .cts requiring esm without default type module', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--no-experimental-require-module',
    fixtures.path('typescript/cts/test-mts-node_modules.cts'),
  ]);

  strictEqual(result.stdout, '');
  match(result.stderr, /ERR_REQUIRE_ESM/);
  strictEqual(result.code, 1);
});

test('expect failure of a .cts file requiring esm in node_modules', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-require-module',
    fixtures.path('typescript/cts/test-mts-node_modules.cts'),
  ]);

  strictEqual(result.stdout, '');
  match(result.stderr, /ERR_UNSUPPORTED_NODE_MODULES_TYPE_STRIPPING/);
  strictEqual(result.code, 1);
});
