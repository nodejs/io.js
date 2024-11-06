'use strict';

const { spawnPromisified } = require('../common');
const fixtures = require('../common/fixtures');
const assert = require('node:assert');
const { resolveLoadAndCache } = require('node:module');
const { describe, it } = require('node:test');


describe('resolveLoadAndCache', () => { // Throws when no arguments are provided
  it('should return null source for CJS module and built-in modules', async () => {
    assert.deepStrictEqual(await resolveLoadAndCache(fixtures.fileURL('empty.js')), {
      __proto__: null,
      url: `${fixtures.fileURL('empty.js')}`,
      format: 'commonjs',
      source: null,
    });
    assert.deepStrictEqual(await resolveLoadAndCache('node:fs'), {
      __proto__: null,
      url: 'node:fs',
      format: 'builtin',
      source: null,
    });
  });

  it('should return full source for ESM module', async () => {
    assert.deepStrictEqual(await resolveLoadAndCache(fixtures.fileURL('es-modules/print-3.mjs')), {
      __proto__: null,
      url: `${fixtures.fileURL('es-modules/print-3.mjs')}`,
      format: 'module',
      source: Buffer.from('console.log(3);\n'),
    });
  });

  it('should accept relative path when a base URL is provided', async () => {
    assert.deepStrictEqual(await resolveLoadAndCache('./print-3.mjs', fixtures.fileURL('es-modules/loop.mjs')), {
      __proto__: null,
      url: `${fixtures.fileURL('es-modules/print-3.mjs')}`,
      format: 'module',
      source: Buffer.from('console.log(3);\n'),
    });
  });

  it('should throw when parentLocation is invalid', async () => {
    for (const invalid of [null, {}, [], () => {}, true, false, 1, 0]) {
      await assert.rejects(
        () => resolveLoadAndCache('', invalid),
        { code: 'ERR_INVALID_URL' },
      );
    }

    await assert.rejects(resolveLoadAndCache('', Symbol()), {
      name: 'TypeError',
    });
  });

  it('should accept a file URL (string), like from `import.meta.resolve()`', async () => {
    const url = `${fixtures.fileURL('es-modules/print-3.mjs')}`;
    assert.deepStrictEqual(await resolveLoadAndCache(url), {
      __proto__: null,
      url,
      format: 'module',
      source: Buffer.from('console.log(3);\n'),
    });
    assert.deepStrictEqual(await resolveLoadAndCache('./print-3.mjs', fixtures.fileURL('es-modules/loop.mjs').href), {
      __proto__: null,
      url,
      format: 'module',
      source: Buffer.from('console.log(3);\n'),
    });
  });

  it('should require import attribute to load JSON files', async () => {
    const url = 'data:application/json,{}';
    await assert.rejects(resolveLoadAndCache(url), { code: 'ERR_IMPORT_ATTRIBUTE_MISSING' });
    assert.deepStrictEqual(await resolveLoadAndCache(url, undefined, { type: 'json' }), {
      __proto__: null,
      format: 'json',
      url,
      source: Buffer.from('{}'),
    });
  });

  it.skip('should use the existing cache', async () => {
    const url = fixtures.fileURL('es-modules/print-3.mjs');
    assert.deepStrictEqual(await spawnPromisified(process.execPath, [
      '--no-warnings',
      '--loader',
      fixtures.fileURL('es-module-loaders/loader-resolve-passthru.mjs'),
      '--loader',
      fixtures.fileURL('es-module-loaders/loader-load-passthru.mjs'),
      '-p',
      `import(${JSON.stringify(url)}).then(() => module.resolveLoadAndCache(${JSON.stringify(url)}, url.pathToFileURL(__filename)))`,
    ]), {
      stderr: '',
      stdout: `Promise <>`,
      code: 0,
      signal: null,
    });
  });

  it.skip('should populate the cache', async () => {});
});
