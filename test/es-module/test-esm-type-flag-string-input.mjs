import { spawnPromisified } from '../common/index.mjs';
import { spawn } from 'node:child_process';
import { describe, it } from 'node:test';
import { strictEqual, match } from 'node:assert';

describe('the type flag should change the interpretation of string input', {
  concurrency: !process.env.TEST_PARALLEL,
}, () => {
  it('should run as ESM input passed via --eval', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(process.execPath, [
      '--experimental-default-type=module',
      '--eval',
      'import "data:text/javascript,console.log(42)"',
    ]);

    strictEqual(stderr, '');
    strictEqual(stdout, '42\n');
    strictEqual(code, 0);
    strictEqual(signal, null);
  });

  // ESM is unsupported for --print via --input-type=module

  it('should run as ESM input passed via STDIN', async () => {
    const child = spawn(process.execPath, [
      '--experimental-default-type=module',
    ]);
    child.stdin.end('console.log(typeof import.meta.resolve)');

    match((await child.stdout.toArray()).toString(), /^function\r?\n$/);
  });

  it('should be overridden by --input-type', async () => {
    const { code, signal, stdout, stderr } = await spawnPromisified(process.execPath, [
      '--experimental-default-type=module',
      '--input-type=commonjs',
      '--eval',
      'console.log(require("process").version)',
    ]);

    strictEqual(stderr, '');
    strictEqual(stdout, `${process.version}\n`);
    strictEqual(code, 0);
    strictEqual(signal, null);
  });
});
