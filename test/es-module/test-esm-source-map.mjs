import { spawnPromisified } from '../common/index.mjs';
import * as fixtures from '../common/fixtures.mjs';
import assert from 'node:assert';
import { execPath } from 'node:process';
import { describe, it } from 'node:test';

describe('esm source-map', { concurrency: !process.env.TEST_PARALLEL }, () => {
  // Issue: https://github.com/nodejs/node/issues/51522

  [
    [
      'in middle from esm',
      'source-map/extract-url/esm-url-in-middle.mjs',
      true,
    ],
    [
      'inside string from esm',
      'source-map/extract-url/esm-url-in-string.mjs',
      false,
    ],
  ].forEach(([name, path, shouldExtract]) => {
    it((shouldExtract ? 'should extract source map url' : 'should not extract source map url') + name, async () => {
      const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
        '--no-warnings',
        '--enable-source-maps',
        fixtures.path(path),
      ]);

      assert.strictEqual(stdout, '');
      if (shouldExtract) {
        assert.match(stderr, /index\.ts/);
      } else {
        assert.doesNotMatch(stderr, /index\.ts/);
      }
      assert.strictEqual(code, 1);
      assert.strictEqual(signal, null);
    });
  });

  [
    [
      'in middle from esm imported by esm',
      `import ${JSON.stringify(fixtures.fileURL('source-map/extract-url/esm-url-in-middle.mjs'))}`,
      true,
    ],
    [
      'in middle from cjs imported by esm',
      `import ${JSON.stringify(fixtures.fileURL('source-map/extract-url/cjs-url-in-middle.js'))}`,
      true,
    ],
    [
      'in middle from cjs required by esm',
      "import { createRequire } from 'module';" +
      'const require = createRequire(import.meta.url);' +
      `require(${JSON.stringify(fixtures.path('source-map/extract-url/cjs-url-in-middle.js'))})`,
      true,
    ],

    [
      'inside string from esm imported by esm',
      `import ${JSON.stringify(fixtures.fileURL('source-map/extract-url/esm-url-in-string.mjs'))}`,
      false,
    ],
    [
      'inside string from cjs imported by esm',
      `import ${JSON.stringify(fixtures.fileURL('source-map/extract-url/cjs-url-in-string.js'))}`,
      false,
    ],
    [
      'inside string from cjs required by esm',
      "import { createRequire } from 'module';" +
      'const require = createRequire(import.meta.url);' +
      `require(${JSON.stringify(fixtures.path('source-map/extract-url/cjs-url-in-string.js'))})`,
      false,
    ],
  ].forEach(([name, evalCode, shouldExtract]) => {
    it((shouldExtract ? 'should extract source map url' : 'should not extract source map url') + name, async () => {
      const { code, signal, stdout, stderr } = await spawnPromisified(execPath, [
        '--no-warnings',
        '--enable-source-maps',
        '--input-type=module',
        '--eval',
        evalCode,
      ]);

      assert.strictEqual(stdout, '');
      if (shouldExtract) {
        assert.match(stderr, /index\.ts/);
      } else {
        assert.doesNotMatch(stderr, /index\.ts/);
      }
      assert.strictEqual(code, 1);
      assert.strictEqual(signal, null);
    });
  });
});
