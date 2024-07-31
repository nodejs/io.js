import { spawnPromisified } from '../common/index.mjs';
import { fileURL, path } from '../common/fixtures.mjs';
import { match, ok, notStrictEqual } from 'node:assert';
import { execPath } from 'node:process';
import { describe, it } from 'node:test';


describe('ESM: loader with syntax error', { concurrency: !process.env.TEST_PARALLEL }, () => {
  it('should crash the node process', async () => {
    const { code, stderr } = await spawnPromisified(execPath, [
      '--experimental-loader',
      fileURL('es-module-loaders', 'syntax-error.mjs').href,
      path('print-error-message.js'),
    ]);

    match(stderr, /SyntaxError \[Error\]:/);
    ok(!stderr.includes('Bad command or file name'));
    notStrictEqual(code, 0);
  });
});
