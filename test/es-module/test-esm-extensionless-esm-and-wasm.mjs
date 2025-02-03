// Flags: --experimental-wasm-modules
import { spawnPromisified } from '../common/index.mjs';
import * as fixtures from '../common/fixtures.mjs';
import { describe, it } from 'node:test';
import { match, strictEqual } from 'node:assert';

describe('extensionless ES modules within a "type": "module" package scope', {
  concurrency: !process.env.TEST_PARALLEL,
}, () => {
  it('should run as the entry point', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(process.execPath, [
      fixtures.path('es-modules/package-type-module/noext-esm'),
    ]);

    strictEqual(stderr, '');
    strictEqual(stdout, 'executed\n');
    strictEqual(code, 0);
    strictEqual(signal, null);
  });

  it('should be importable', async () => {
    const { default: defaultExport } =
              await import(fixtures.fileURL('es-modules/package-type-module/noext-esm'));
    strictEqual(defaultExport, 'module');
  });

  it('should be importable from a module scope under node_modules', async () => {
    const { default: defaultExport } =
      await import(fixtures.fileURL(
        'es-modules/package-type-module/node_modules/dep-with-package-json-type-module/noext-esm'));
    strictEqual(defaultExport, 'module');
  });
});
describe('extensionless Wasm modules within a "type": "module" package scope', {
  concurrency: !process.env.TEST_PARALLEL,
}, () => {
  it('should run as the entry point', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(process.execPath, [
      '--experimental-wasm-modules',
      '--no-warnings',
      fixtures.path('es-modules/package-type-module/noext-wasm'),
    ]);

    strictEqual(stderr, '');
    strictEqual(stdout, 'executed\n');
    strictEqual(code, 0);
    strictEqual(signal, null);
  });

  it('should be importable', async () => {
    const { add } = await import(fixtures.fileURL('es-modules/package-type-module/noext-wasm'));
    strictEqual(add(1, 2), 3);
  });

  it('should be importable from a module scope under node_modules', async () => {
    const { add } = await import(fixtures.fileURL(
      'es-modules/package-type-module/node_modules/dep-with-package-json-type-module/noext-wasm'));
    strictEqual(add(1, 2), 3);
  });
});

describe('extensionless ES modules within no package scope', { concurrency: !process.env.TEST_PARALLEL }, () => {
  it('should run as the entry point', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(process.execPath, [
      fixtures.path('es-modules/noext-esm'),
    ]);

    strictEqual(stdout, 'executed\n');
    strictEqual(stderr, '');
    strictEqual(code, 0);
    strictEqual(signal, null);
  });

  it('should run on import', async () => {
    await import(fixtures.fileURL('es-modules/noext-esm'));
  });
});

describe('extensionless Wasm within no package scope', { concurrency: !process.env.TEST_PARALLEL }, () => {
  it('should error as the entry point', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(process.execPath, [
      '--experimental-wasm-modules',
      '--no-warnings',
      fixtures.path('es-modules/noext-wasm'),
    ]);

    match(stderr, /SyntaxError/);
    strictEqual(stdout, '');
    strictEqual(code, 1);
    strictEqual(signal, null);
  });

  it('should run on import', async () => {
    await import(fixtures.fileURL('es-modules/noext-wasm'));
  });
});
