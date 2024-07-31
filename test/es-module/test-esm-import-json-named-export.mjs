import { spawnPromisified } from '../common/index.mjs';
import * as fixtures from '../common/fixtures.mjs';
import assert from 'node:assert';
import { execPath } from 'node:process';
import { describe, it } from 'node:test';


describe('ESM: named JSON exports', { concurrency: !process.env.TEST_PARALLEL }, () => {
  it('should throw, citing named import', async () => {
    const { code, stderr } = await spawnPromisified(execPath, [
      fixtures.path('es-modules', 'import-json-named-export.mjs'),
    ]);

    // SyntaxError: The requested module '../experimental.json'
    // does not provide an export named 'ofLife'
    assert.match(stderr, /SyntaxError:/);
    assert.match(stderr, /'\.\.\/experimental\.json'/);
    assert.match(stderr, /'ofLife'/);

    assert.notStrictEqual(code, 0);
  });
});
