// Flags: --experimental-default-type=module --experimental-wasm-modules
import { spawnPromisified } from '../common/index.mjs';
import * as fixtures from '../common/fixtures.mjs';
import { describe, it } from 'node:test';
import { strictEqual } from 'node:assert';

describe('the type flag should change the interpretation of certain files outside of any package scope',
         { concurrency: !process.env.TEST_PARALLEL }, () => {
           it('should run as ESM a .js file that is outside of any package scope', async () => {
             const { code, signal, stdout, stderr } = await spawnPromisified(process.execPath, [
               '--experimental-default-type=module',
               fixtures.path('es-modules/loose.js'),
             ]);

             strictEqual(stderr, '');
             strictEqual(stdout, 'executed\n');
             strictEqual(code, 0);
             strictEqual(signal, null);
           });

           it('should run as ESM an extensionless JavaScript file that is outside of any package scope', async () => {
             const { code, signal, stdout, stderr } = await spawnPromisified(process.execPath, [
               '--experimental-default-type=module',
               fixtures.path('es-modules/noext-esm'),
             ]);

             strictEqual(stderr, '');
             strictEqual(stdout, 'executed\n');
             strictEqual(code, 0);
             strictEqual(signal, null);
           });

           it('should run as Wasm an extensionless Wasm file that is outside of any package scope', async () => {
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

           it('should import as ESM a .js file that is outside of any package scope', async () => {
             const { default: defaultExport } = await import(fixtures.fileURL('es-modules/loose.js'));
             strictEqual(defaultExport, 'module');
           });

           it('should import as ESM an extensionless JavaScript file that is outside of any package scope',
              async () => {
                const { default: defaultExport } = await import(fixtures.fileURL('es-modules/noext-esm'));
                strictEqual(defaultExport, 'module');
              });

           it('should import as Wasm an extensionless Wasm file that is outside of any package scope', async () => {
             const { add } = await import(fixtures.fileURL('es-modules/noext-wasm'));
             strictEqual(add(1, 2), 3);
           });

           it('should check as ESM input passed via --check', async () => {
             const { code, signal, stdout, stderr } = await spawnPromisified(process.execPath, [
               '--experimental-default-type=module',
               '--check',
               fixtures.path('es-modules/loose.js'),
             ]);

             strictEqual(stderr, '');
             strictEqual(stdout, '');
             strictEqual(code, 0);
             strictEqual(signal, null);
           });
         });
