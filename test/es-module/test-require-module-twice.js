// Flags: --experimental-require-module
'use strict';

require('../common');
const assert = require('assert');

const modules = [
  '../fixtures/es-module-loaders/module-named-exports.mjs',
  '../fixtures/es-modules/import-esm.mjs',
  '../fixtures/es-modules/require-cjs.mjs',
  '../fixtures/es-modules/cjs-exports.mjs',
  '../common/index.mjs',
  '../fixtures/es-modules/package-type-module/index.js',
];

for (const id of modules) {
  const first = require(id);
  const second = require(id);
  assert.strictEqual(first, second,
                     `the results of require('${id}') twice are not reference equal`);
}
