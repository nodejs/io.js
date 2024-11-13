// Previously, this tested that require(esm) throws ERR_REQUIRE_ESM, which is no longer applicable
// since require(esm) is now supported. The test has been repurposed to ensure that the old behavior
// is preserved when the --no-experimental-require-module flag is used.
'use strict';

const { spawnPromisified } = require('../common');
const fixtures = require('../common/fixtures.js');
const assert = require('node:assert');
const path = require('node:path');
const { execPath } = require('node:process');
const { describe, it } = require('node:test');


const requiringCjsAsEsm = path.resolve(fixtures.path('/es-modules/cjs-esm.js'));
const requiringEsm = path.resolve(fixtures.path('/es-modules/cjs-esm-esm.js'));
const pjson = path.toNamespacedPath(
  fixtures.path('/es-modules/package-type-module/package.json')
);


describe('CJS ↔︎ ESM interop warnings', { concurrency: !process.env.TEST_PARALLEL }, () => {

  it(async () => {
    const required = path.resolve(
      fixtures.path('/es-modules/package-type-module/cjs.js')
    );
    const basename = 'cjs.js';
    const { code, signal, stderr } = await spawnPromisified(execPath, [
      '--no-experimental-require-module', requiringCjsAsEsm,
    ]);

    assert.ok(
      stderr.replaceAll('\r', '').includes(
        `Error [ERR_REQUIRE_ESM]: require() of ES Module ${required} from ${requiringCjsAsEsm} not supported.\n`
      )
    );
    assert.ok(
      stderr.replaceAll('\r', '').includes(
        `Instead either rename ${basename} to end in .cjs, change the requiring ` +
        'code to use dynamic import() which is available in all CommonJS ' +
        `modules, or change "type": "module" to "type": "commonjs" in ${pjson} to ` +
        'treat all .js files as CommonJS (using .mjs for all ES modules ' +
        'instead).\n'
      )
    );

    assert.strictEqual(code, 1);
    assert.strictEqual(signal, null);
  });

  it(async () => {
    const required = path.resolve(
      fixtures.path('/es-modules/package-type-module/esm.js')
    );
    const basename = 'esm.js';
    const { code, signal, stderr } = await spawnPromisified(execPath, [
      '--no-experimental-require-module', requiringEsm,
    ]);

    assert.ok(
      stderr.replace(/\r/g, '').includes(
        `Error [ERR_REQUIRE_ESM]: require() of ES Module ${required} from ${requiringEsm} not supported.\n`
      )
    );
    assert.ok(
      stderr.replace(/\r/g, '').includes(
        `Instead change the require of ${basename} in ${requiringEsm} to` +
        ' a dynamic import() which is available in all CommonJS modules.\n'
      )
    );

    assert.strictEqual(code, 1);
    assert.strictEqual(signal, null);
  });
});
