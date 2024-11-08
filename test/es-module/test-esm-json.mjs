import { spawnPromisified } from '../common/index.mjs';
import * as fixtures from '../common/fixtures.mjs';
import assert from 'node:assert';
import { execPath } from 'node:process';
import { before, describe, it, test } from 'node:test';

import { mkdir, rm, writeFile } from 'node:fs/promises';
import * as tmpdir from '../common/tmpdir.js';

import secret from '../fixtures/experimental.json' with { type: 'json' };

describe('ESM: importing JSON', () => {
  before(() => tmpdir.refresh());

  it('should load JSON', () => {
    assert.strictEqual(secret.ofLife, 42);
  });

  it('should not print an experimental warning', async () => {
    const { code, signal, stderr } = await spawnPromisified(execPath, [
      fixtures.path('/es-modules/json-modules.mjs'),
    ]);

    assert.strictEqual(stderr, '');
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);
  });

  test('should load different modules when the URL is different', async (t) => {
    const root = tmpdir.fileURL(`./test-esm-json-${Math.random()}/`);
    try {
      await mkdir(root, { recursive: true });

      await t.test('json', async () => {
        let i = 0;
        const url = new URL('./foo.json', root);
        await writeFile(url, JSON.stringify({ id: i++ }));
        const absoluteURL = await import(`${url}`, {
          with: { type: 'json' },
        });
        await writeFile(url, JSON.stringify({ id: i++ }));
        const queryString = await import(`${url}?a=2`, {
          with: { type: 'json' },
        });
        await writeFile(url, JSON.stringify({ id: i++ }));
        const hash = await import(`${url}#a=2`, {
          with: { type: 'json' },
        });
        await writeFile(url, JSON.stringify({ id: i++ }));
        const queryStringAndHash = await import(`${url}?a=2#a=2`, {
          with: { type: 'json' },
        });

        assert.notDeepStrictEqual(absoluteURL, queryString);
        assert.notDeepStrictEqual(absoluteURL, hash);
        assert.notDeepStrictEqual(queryString, hash);
        assert.notDeepStrictEqual(absoluteURL, queryStringAndHash);
        assert.notDeepStrictEqual(queryString, queryStringAndHash);
        assert.notDeepStrictEqual(hash, queryStringAndHash);
      });

      await t.test('js', async () => {
        let i = 0;
        const url = new URL('./foo.mjs', root);
        await writeFile(url, `export default ${JSON.stringify({ id: i++ })}\n`);
        const absoluteURL = await import(`${url}`);
        await writeFile(url, `export default ${JSON.stringify({ id: i++ })}\n`);
        const queryString = await import(`${url}?a=1`);
        await writeFile(url, `export default ${JSON.stringify({ id: i++ })}\n`);
        const hash = await import(`${url}#a=1`);
        await writeFile(url, `export default ${JSON.stringify({ id: i++ })}\n`);
        const queryStringAndHash = await import(`${url}?a=1#a=1`);

        assert.notDeepStrictEqual(absoluteURL, queryString);
        assert.notDeepStrictEqual(absoluteURL, hash);
        assert.notDeepStrictEqual(queryString, hash);
        assert.notDeepStrictEqual(absoluteURL, queryStringAndHash);
        assert.notDeepStrictEqual(queryString, queryStringAndHash);
        assert.notDeepStrictEqual(hash, queryStringAndHash);
      });
    } finally {
      await rm(root, { force: true, recursive: true });
    }
  });
});
