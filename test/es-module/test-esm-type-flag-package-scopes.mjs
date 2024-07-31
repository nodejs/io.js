// Flags: --experimental-default-type=module --experimental-wasm-modules
import { spawnPromisified } from '../common/index.mjs';
import * as fixtures from '../common/fixtures.mjs';
import { describe, it } from 'node:test';
import { strictEqual } from 'node:assert';

describe('the type flag should change the interpretation of certain files within a "type": "module" package scope',
         { concurrency: !process.env.TEST_PARALLEL }, () => {
           it('should run as ESM an extensionless JavaScript file within a "type": "module" scope', async () => {
             const { code, signal, stdout, stderr } = await spawnPromisified(process.execPath, [
               '--experimental-default-type=module',
               fixtures.path('es-modules/package-type-module/noext-esm'),
             ]);

             strictEqual(stderr, '');
             strictEqual(stdout, 'executed\n');
             strictEqual(code, 0);
             strictEqual(signal, null);
           });

           it('should import an extensionless JavaScript file within a "type": "module" scope', async () => {
             const { default: defaultExport } =
              await import(fixtures.fileURL('es-modules/package-type-module/noext-esm'));
             strictEqual(defaultExport, 'module');
           });

           it('should import an extensionless JavaScript file within a "type": "module" scope under node_modules',
              async () => {
                const { default: defaultExport } =
                await import(fixtures.fileURL(
                  'es-modules/package-type-module/node_modules/dep-with-package-json-type-module/noext-esm'));
                strictEqual(defaultExport, 'module');
              });

           it('should run as Wasm an extensionless Wasm file within a "type": "module" scope', async () => {
             const { code, signal, stdout, stderr } = await spawnPromisified(process.execPath, [
               '--experimental-default-type=module',
               '--experimental-wasm-modules',
               '--no-warnings',
               fixtures.path('es-modules/package-type-module/noext-wasm'),
             ]);

             strictEqual(stderr, '');
             strictEqual(stdout, 'executed\n');
             strictEqual(code, 0);
             strictEqual(signal, null);
           });

           it('should import as Wasm an extensionless Wasm file within a "type": "module" scope', async () => {
             const { add } = await import(fixtures.fileURL('es-modules/package-type-module/noext-wasm'));
             strictEqual(add(1, 2), 3);
           });

           it('should import an extensionless Wasm file within a "type": "module" scope under node_modules',
              async () => {
                const { add } = await import(fixtures.fileURL(
                  'es-modules/package-type-module/node_modules/dep-with-package-json-type-module/noext-wasm'));
                strictEqual(add(1, 2), 3);
              });
         });

describe(`the type flag should change the interpretation of certain files within a package scope that lacks a
"type" field and is not under node_modules`, { concurrency: !process.env.TEST_PARALLEL }, () => {
  it('should run as ESM a .js file within package scope that has no defined "type" and is not under node_modules',
     async () => {
       const { code, signal, stdout, stderr } = await spawnPromisified(process.execPath, [
         '--experimental-default-type=module',
         fixtures.path('es-modules/package-without-type/module.js'),
       ]);

       strictEqual(stderr, '');
       strictEqual(stdout, 'executed\n');
       strictEqual(code, 0);
       strictEqual(signal, null);
     });

  it(`should run as ESM an extensionless JavaScript file within a package scope that has no defined "type" and is not
under node_modules`, async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(process.execPath, [
      '--experimental-default-type=module',
      fixtures.path('es-modules/package-without-type/noext-esm'),
    ]);

    strictEqual(stderr, '');
    strictEqual(stdout, 'executed\n');
    strictEqual(code, 0);
    strictEqual(signal, null);
  });

  it(`should run as Wasm an extensionless Wasm file within a package scope that has no defined "type" and is not under
  node_modules`, async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(process.execPath, [
      '--experimental-default-type=module',
      '--experimental-wasm-modules',
      '--no-warnings',
      fixtures.path('es-modules/noext-wasm'),
    ]);

    strictEqual(stderr, '');
    strictEqual(stdout, '');
    strictEqual(code, 0);
    strictEqual(signal, null);
  });

  it('should import as ESM a .js file within package scope that has no defined "type" and is not under node_modules',
     async () => {
       const { default: defaultExport } = await import(fixtures.fileURL('es-modules/package-without-type/module.js'));
       strictEqual(defaultExport, 'module');
     });

  it(`should import as ESM an extensionless JavaScript file within a package scope that has no defined "type" and is
  not under node_modules`, async () => {
    const { default: defaultExport } = await import(fixtures.fileURL('es-modules/package-without-type/noext-esm'));
    strictEqual(defaultExport, 'module');
  });

  it(`should import as Wasm an extensionless Wasm file within a package scope that has no defined "type" and is not
  under node_modules`, async () => {
    const { add } = await import(fixtures.fileURL('es-modules/noext-wasm'));
    strictEqual(add(1, 2), 3);
  });
});

describe(`the type flag should NOT change the interpretation of certain files within a package scope that lacks a
"type" field and is under node_modules`, { concurrency: !process.env.TEST_PARALLEL }, () => {
  it('should run as CommonJS a .js file within package scope that has no defined "type" and is under node_modules',
     async () => {
       const { code, signal, stdout, stderr } = await spawnPromisified(process.execPath, [
         '--experimental-default-type=module',
         fixtures.path('es-modules/package-type-module/node_modules/dep-with-package-json-without-type/run.js'),
       ]);

       strictEqual(stderr, '');
       strictEqual(stdout, 'executed\n');
       strictEqual(code, 0);
       strictEqual(signal, null);
     });

  it(`should import as CommonJS a .js file within a package scope that has no defined "type" and is under
  node_modules`, async () => {
    const { default: defaultExport } =
      await import(fixtures.fileURL(
        'es-modules/package-type-module/node_modules/dep-with-package-json-without-type/run.js'));
    strictEqual(defaultExport, 42);
  });

  it(`should run as CommonJS an extensionless JavaScript file within a package scope that has no defined "type" and is
  under node_modules`, async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(process.execPath, [
      '--experimental-default-type=module',
      fixtures.path('es-modules/package-type-module/node_modules/dep-with-package-json-without-type/noext-cjs'),
    ]);

    strictEqual(stderr, '');
    strictEqual(stdout, 'executed\n');
    strictEqual(code, 0);
    strictEqual(signal, null);
  });

  it(`should import as CommonJS an extensionless JavaScript file within a package scope that has no defined "type" and
  is under node_modules`, async () => {
    const { default: defaultExport } =
      await import(fixtures.fileURL(
        'es-modules/package-type-module/node_modules/dep-with-package-json-without-type/noext-cjs'));
    strictEqual(defaultExport, 42);
  });
});
