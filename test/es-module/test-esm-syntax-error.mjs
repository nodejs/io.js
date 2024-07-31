import { spawnPromisified } from '../common/index.mjs';
import { path } from '../common/fixtures.mjs';
import { match, notStrictEqual } from 'node:assert';
import { execPath } from 'node:process';
import { describe, it } from 'node:test';


describe('ESM: importing a module with syntax error(s)', { concurrency: !process.env.TEST_PARALLEL }, () => {
  it('should throw', async () => {
    const { code, stderr } = await spawnPromisified(execPath, [
      path('es-module-loaders', 'syntax-error.mjs'),
    ]);
    match(stderr, /SyntaxError:/);
    notStrictEqual(code, 0);
  });
});
