import { spawnPromisified } from '../common/index.mjs';
import * as fixtures from '../common/fixtures.mjs';
import { describe, it } from 'node:test';
import { deepStrictEqual, match, strictEqual } from 'node:assert';

describe('--experimental-default-type=module', { concurrency: !process.env.TEST_PARALLEL }, () => {
  describe('should not affect the interpretation of files with unknown extensions', {
    concurrency: !process.env.TEST_PARALLEL,
  }, () => {
    it('should error on an entry point with an unknown extension', async () => {
      const { code, signal, stdout, stderr } = await spawnPromisified(process.execPath, [
        '--experimental-default-type=module',
        fixtures.path('es-modules/package-type-module/extension.unknown'),
      ]);

      match(stderr, /ERR_UNKNOWN_FILE_EXTENSION/);
      strictEqual(stdout, '');
      strictEqual(code, 1);
      strictEqual(signal, null);
    });

    it('should error on an import with an unknown extension', async () => {
      const { code, signal, stdout, stderr } = await spawnPromisified(process.execPath, [
        '--experimental-default-type=module',
        fixtures.path('es-modules/package-type-module/imports-unknownext.mjs'),
      ]);

      match(stderr, /ERR_UNKNOWN_FILE_EXTENSION/);
      strictEqual(stdout, '');
      strictEqual(code, 1);
      strictEqual(signal, null);
    });
  });

  it('should affect CJS .js files (imported, required, entry points)', async () => {
    const result = await spawnPromisified(process.execPath, [
      '--experimental-default-type=module',
      fixtures.path('es-modules/package-type-commonjs/echo-require-cache.js'),
    ]);

    deepStrictEqual(result, {
      code: 0,
      stderr: '',
      stdout: 'undefined\n',
      signal: null,
    });
  });

  it('should affect .cjs files that are imported', async () => {
    const result = await spawnPromisified(process.execPath, [
      '--experimental-default-type=module',
      '-e',
      `import ${JSON.stringify(fixtures.fileURL('es-module-require-cache/echo.cjs'))}`,
    ]);

    deepStrictEqual(result, {
      code: 0,
      stderr: '',
      stdout: 'undefined\n',
      signal: null,
    });
  });

  it('should affect entry point .cjs files (with no hooks)', async () => {
    const { stderr, stdout, code } = await spawnPromisified(process.execPath, [
      '--experimental-default-type=module',
      fixtures.path('es-module-require-cache/echo.cjs'),
    ]);

    strictEqual(stderr, '');
    match(stdout, /^undefined\n$/);
    strictEqual(code, 0);
  });

  it('should affect entry point .cjs files (when any hooks is registered)', async () => {
    const result = await spawnPromisified(process.execPath, [
      '--experimental-default-type=module',
      '--import',
      'data:text/javascript,import{register}from"node:module";register("data:text/javascript,");',
      fixtures.path('es-module-require-cache/echo.cjs'),
    ]);

    deepStrictEqual(result, {
      code: 0,
      stderr: '',
      stdout: 'undefined\n',
      signal: null,
    });
  });

  it('should not affect CJS from input-type', async () => {
    const { stderr, stdout, code } = await spawnPromisified(process.execPath, [
      '--experimental-default-type=module',
      '--input-type=commonjs',
      '-p',
      'require.cache',
    ]);

    strictEqual(stderr, '');
    match(stdout, /^\[Object: null prototype\] \{\}\n$/);
    strictEqual(code, 0);
  });
});
