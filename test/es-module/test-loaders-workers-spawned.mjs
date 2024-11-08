import { spawnPromisified } from '../common/index.mjs';
import * as fixtures from '../common/fixtures.mjs';
import assert from 'node:assert';
import { execPath } from 'node:process';
import { describe, it } from 'node:test';

describe('Worker threads do not spawn infinitely', { concurrency: !process.env.TEST_PARALLEL }, () => {
  it('should not trigger an infinite loop when using a loader exports no recognized hooks', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      '--no-warnings',
      '--experimental-loader',
      fixtures.fileURL('empty.js'),
      '--eval',
      'setTimeout(() => console.log("hello"),99)',
    ]);

    assert.strictEqual(stderr, '');
    assert.match(stdout, /^hello\r?\n$/);
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);
  });

  it('should support a CommonJS entry point and a loader that imports a CommonJS module', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      '--no-warnings',
      '--experimental-loader',
      fixtures.fileURL('es-module-loaders/loader-with-dep.mjs'),
      fixtures.path('print-delayed.js'),
    ]);

    assert.strictEqual(stderr, '');
    assert.match(stdout, /^delayed\r?\n$/);
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);
  });

  it('should support --require and --import along with using a loader written in CJS and CJS entry point', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      '--no-warnings',
      '--eval',
      'setTimeout(() => console.log("D"),99)',
      '--import',
      fixtures.fileURL('printC.js'),
      '--experimental-loader',
      fixtures.fileURL('printB.js'),
      '--require',
      fixtures.path('printA.js'),
    ]);

    assert.strictEqual(stderr, '');
    // We are validating that:
    // 1. the `--require` flag is run first from the main thread (and A is printed).
    // 2. the `--require` flag is then run on the loader thread (and A is printed).
    // 3. the `--loader` module is executed (and B is printed).
    // 4. the `--import` module is evaluated once, on the main thread (and C is printed).
    // 5. the user code is finally executed (and D is printed).
    // The worker code should always run before the --import, but the console.log might arrive late.
    assert.match(stdout, /^A\r?\n(A\r?\nB\r?\nC|A\r?\nC\r?\nB|C\r?\nA\r?\nB)\r?\nD\r?\n$/);
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);
  });

  it('should support --require and --import along with using a loader written in ESM and ESM entry point', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
      '--no-warnings',
      '--require',
      fixtures.path('printA.js'),
      '--experimental-loader',
      'data:text/javascript,console.log("B")',
      '--import',
      fixtures.fileURL('printC.js'),
      '--input-type=module',
      '--eval',
      'setTimeout(() => console.log("D"),99)',
    ]);

    assert.strictEqual(stderr, '');
    // The worker code should always run before the --import, but the console.log might arrive late.
    assert.match(stdout, /^A\r?\n(A\r?\nB\r?\nC|A\r?\nC\r?\nB|C\r?\nA\r?\nB)\r?\nD\r?\n$/);
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);
  });
});
