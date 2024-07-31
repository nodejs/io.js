import { spawnPromisified } from '../common/index.mjs';
import * as fixtures from '../common/fixtures.mjs';
import { describe, it } from 'node:test';
import { match, strictEqual } from 'node:assert';

describe('--experimental-default-type=module should not support extension searching', {
  concurrency: !process.env.TEST_PARALLEL,
}, () => {
  it('should support extension searching under --experimental-default-type=commonjs', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(process.execPath, [
      '--experimental-default-type=commonjs',
      'index',
    ], {
      cwd: fixtures.path('es-modules/package-without-type'),
    });

    strictEqual(stdout, 'package-without-type\n');
    strictEqual(stderr, '');
    strictEqual(code, 0);
    strictEqual(signal, null);
  });

  it('should error with implicit extension under --experimental-default-type=module', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(process.execPath, [
      '--experimental-default-type=module',
      'index',
    ], {
      cwd: fixtures.path('es-modules/package-without-type'),
    });

    match(stderr, /ENOENT.*Did you mean to import .*index\.js"\?/s);
    strictEqual(stdout, '');
    strictEqual(code, 1);
    strictEqual(signal, null);
  });
});

describe('--experimental-default-type=module should not parse paths as URLs', {
  concurrency: !process.env.TEST_PARALLEL,
}, () => {
  it('should not parse a `?` in a filename as starting a query string', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(process.execPath, [
      '--experimental-default-type=module',
      'file#1.js',
    ], {
      cwd: fixtures.path('es-modules/package-without-type'),
    });

    strictEqual(stderr, '');
    strictEqual(stdout, 'file#1\n');
    strictEqual(code, 0);
    strictEqual(signal, null);
  });

  it('should resolve `..`', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(process.execPath, [
      '--experimental-default-type=module',
      '../package-without-type/file#1.js',
    ], {
      cwd: fixtures.path('es-modules/package-without-type'),
    });

    strictEqual(stderr, '');
    strictEqual(stdout, 'file#1\n');
    strictEqual(code, 0);
    strictEqual(signal, null);
  });

  it('should allow a leading `./`', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(process.execPath, [
      '--experimental-default-type=module',
      './file#1.js',
    ], {
      cwd: fixtures.path('es-modules/package-without-type'),
    });

    strictEqual(stderr, '');
    strictEqual(stdout, 'file#1\n');
    strictEqual(code, 0);
    strictEqual(signal, null);
  });

  it('should not require a leading `./`', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(process.execPath, [
      '--experimental-default-type=module',
      'file#1.js',
    ], {
      cwd: fixtures.path('es-modules/package-without-type'),
    });

    strictEqual(stderr, '');
    strictEqual(stdout, 'file#1\n');
    strictEqual(code, 0);
    strictEqual(signal, null);
  });
});
