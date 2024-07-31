import { spawnPromisified } from '../common/index.mjs';
import { fileURL, path } from '../common/fixtures.mjs';
import { match, ok, notStrictEqual, strictEqual } from 'assert';
import { execPath } from 'node:process';
import { describe, it } from 'node:test';


describe('ESM: thenable loader hooks', { concurrency: !process.env.TEST_PARALLEL }, () => {
  it('should behave as a normal promise resolution', async () => {
    const { code, stderr } = await spawnPromisified(execPath, [
      '--experimental-loader',
      fileURL('es-module-loaders', 'thenable-load-hook.mjs').href,
      path('es-modules', 'test-esm-ok.mjs'),
    ]);

    strictEqual(code, 0);
    ok(!stderr.includes('must not call'));
  });

  it('should crash the node process rejection with an error', async () => {
    const { code, stderr } = await spawnPromisified(execPath, [
      '--experimental-loader',
      fileURL('es-module-loaders', 'thenable-load-hook-rejected.mjs').href,
      path('es-modules', 'test-esm-ok.mjs'),
    ]);

    notStrictEqual(code, 0);
    match(stderr, /\sError: must crash the process\r?\n/);
    ok(!stderr.includes('must not call'));
  });

  it('should just reject without an error (but NOT crash the node process)', async () => {
    const { code, stderr } = await spawnPromisified(execPath, [
      '--experimental-loader',
      fileURL('es-module-loaders', 'thenable-load-hook-rejected-no-arguments.mjs').href,
      path('es-modules', 'test-esm-ok.mjs'),
    ]);

    notStrictEqual(code, 0);
    match(stderr, /\sundefined\r?\n/);
    ok(!stderr.includes('must not call'));
  });
});
