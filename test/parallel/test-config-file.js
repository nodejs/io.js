'use strict';

const { spawnPromisified } = require('../common');
const fixtures = require('../common/fixtures');
const { match, strictEqual } = require('node:assert');
const { test } = require('node:test');

test('should handle non existing json', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-config-file',
    'i-do-not-exist.json',
    '-p', '"Hello, World!"',
  ]);
  match(result.stderr, /Cannot read configuration from i-do-not-exist\.json: no such file or directory/);
  match(result.stderr, /i-do-not-exist\.json: not found/);
  strictEqual(result.stdout, '');
  strictEqual(result.code, 9);
});

test('should handle empty json', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-config-file',
    fixtures.path('rc/empty.json'),
    '-p', '"Hello, World!"',
  ]);
  match(result.stderr, /Can't parse/);
  match(result.stderr, /empty\.json: invalid content/);
  strictEqual(result.stdout, '');
  strictEqual(result.code, 9);
});

test('should handle empty object json', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--no-warnings',
    '--experimental-config-file',
    fixtures.path('rc/empty-object.json'),
    '-p', '"Hello, World!"',
  ]);
  strictEqual(result.stderr, '');
  match(result.stdout, /Hello, World!/);
  strictEqual(result.code, 0);
});

test('should parse boolean flag', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--experimental-config-file',
    fixtures.path('rc/transform-types.json'),
    fixtures.path('typescript/ts/transformation/test-enum.ts'),
  ]);
  match(result.stderr, /--experimental-config-file is an experimental feature and might change at any time/);
  match(result.stdout, /Hello, TypeScript!/);
  strictEqual(result.code, 0);
});

test('should not override a flag declared twice', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--no-warnings',
    '--experimental-config-file',
    fixtures.path('rc/override-property.json'),
    fixtures.path('typescript/ts/transformation/test-enum.ts'),
  ]);
  strictEqual(result.stderr, '');
  strictEqual(result.stdout, 'Hello, TypeScript!\n');
  strictEqual(result.code, 0);
});

test('should override env-file', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--no-warnings',
    '--experimental-config-file',
    fixtures.path('rc/transform-types.json'),
    '--env-file', fixtures.path('dotenv/node-options-no-tranform.env'),
    fixtures.path('typescript/ts/transformation/test-enum.ts'),
  ]);
  strictEqual(result.stderr, '');
  match(result.stdout, /Hello, TypeScript!/);
  strictEqual(result.code, 0);
});

test('should not override NODE_OPTIONS', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--no-warnings',
    '--experimental-config-file',
    fixtures.path('rc/transform-types.json'),
    fixtures.path('typescript/ts/transformation/test-enum.ts'),
  ], {
    env: {
      ...process.env,
      NODE_OPTIONS: '--no-experimental-transform-types',
    },
  });
  match(result.stderr, /ERR_UNSUPPORTED_TYPESCRIPT_SYNTAX/);
  strictEqual(result.stdout, '');
  strictEqual(result.code, 1);
});

test('should not ovverride CLI flags', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--no-warnings',
    '--no-experimental-transform-types',
    '--experimental-config-file',
    fixtures.path('rc/transform-types.json'),
    fixtures.path('typescript/ts/transformation/test-enum.ts'),
  ]);
  match(result.stderr, /ERR_UNSUPPORTED_TYPESCRIPT_SYNTAX/);
  strictEqual(result.stdout, '');
  strictEqual(result.code, 1);
});

test('should parse array flag correctly', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--no-warnings',
    '--experimental-config-file',
    fixtures.path('rc/import.json'),
    '--eval', 'setTimeout(() => console.log("D"),99)',
  ]);
  strictEqual(result.stderr, '');
  strictEqual(result.stdout, 'A\nB\nC\nD\n');
  strictEqual(result.code, 0);
});

test('should validate invalid array flag', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--no-warnings',
    '--experimental-config-file',
    fixtures.path('rc/invalid-import.json'),
    '--eval', 'setTimeout(() => console.log("D"),99)',
  ]);
  match(result.stderr, /invalid-import\.json: invalid content/);
  strictEqual(result.stdout, '');
  strictEqual(result.code, 9);
});

test('should validate array flag as string', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--no-warnings',
    '--experimental-config-file',
    fixtures.path('rc/import-as-string.json'),
    '--eval', 'setTimeout(() => console.log("B"),99)',
  ]);
  strictEqual(result.stderr, '');
  strictEqual(result.stdout, 'A\nB\n');
  strictEqual(result.code, 0);
});

test('should throw at unknown flag', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--no-warnings',
    '--experimental-config-file',
    fixtures.path('rc/unknown-flag.json'),
    '-p', '"Hello, World!"',
  ]);
  match(result.stderr, /Unknown or not allowed option some-unknown-flag/);
  strictEqual(result.stdout, '');
  strictEqual(result.code, 9);
});

test('should throw at flag not available in NODE_OPTIONS', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--no-warnings',
    '--experimental-config-file',
    fixtures.path('rc/not-node-options-flag.json'),
    '-p', '"Hello, World!"',
  ]);
  match(result.stderr, /Unknown or not allowed option --test/);
  strictEqual(result.stdout, '');
  strictEqual(result.code, 9);
});

test('unsigned flag should be parsed correctly', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--no-warnings',
    '--experimental-config-file',
    fixtures.path('rc/numeric.json'),
    '-p', 'http.maxHeaderSize',
  ]);
  strictEqual(result.stderr, '');
  strictEqual(result.stdout, '4294967295\n');
  strictEqual(result.code, 0);
});

test('numeric flag should not allow negative values', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--no-warnings',
    '--experimental-config-file',
    fixtures.path('rc/negative-numeric.json'),
    '-p', 'http.maxHeaderSize',
  ]);
  match(result.stderr, /Invalid value for --max-http-header-size/);
  match(result.stderr, /negative-numeric\.json: invalid content/);
  strictEqual(result.stdout, '');
  strictEqual(result.code, 9);
});

test('v8 flag should not be allowed in config file', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--no-warnings',
    '--experimental-config-file',
    fixtures.path('rc/v8-flag.json'),
    '-p', '"Hello, World!"',
  ]);
  match(result.stderr, /V8 flag --abort-on-uncaught-exception is currently not supported/);
  strictEqual(result.stdout, '');
  strictEqual(result.code, 9);
});

test('string flag should be parsed correctly', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--no-warnings',
    '--test',
    '--experimental-config-file',
    fixtures.path('rc/string.json'),
    fixtures.path('rc/test.js'),
  ]);
  strictEqual(result.stderr, '');
  strictEqual(result.stdout, '.\n');
  strictEqual(result.code, 0);
});

test('host port flag should be parsed correctly', { skip: !process.features.inspector }, async () => {
  const result = await spawnPromisified(process.execPath, [
    '--no-warnings',
    '--expose-internals',
    '--experimental-config-file',
    fixtures.path('rc/host-port.json'),
    '-p', 'require("internal/options").getOptionValue("--inspect-port").port',
  ]);
  strictEqual(result.stderr, '');
  strictEqual(result.stdout, '65535\n');
  strictEqual(result.code, 0);
});

test('no op flag should throw', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--no-warnings',
    '--experimental-config-file',
    fixtures.path('rc/no-op.json'),
    '-p', '"Hello, World!"',
  ]);
  match(result.stderr, /No-op flag --http-parser is currently not supported/);
  match(result.stderr, /no-op\.json: invalid content/);
  strictEqual(result.stdout, '');
  strictEqual(result.code, 9);
});

test('should not allow users to sneak in a flag', async () => {
  const result = await spawnPromisified(process.execPath, [
    '--no-warnings',
    '--experimental-config-file',
    fixtures.path('rc/sneaky-flag.json'),
    '-p', '"Hello, World!"',
  ]);
  match(result.stderr, /The number of NODE_OPTIONS doesn't match the number of flags in the config file/);
  strictEqual(result.stdout, '');
  strictEqual(result.code, 9);
});
