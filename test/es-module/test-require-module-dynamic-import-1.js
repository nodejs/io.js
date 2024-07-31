// Flags: --experimental-require-module
'use strict';

// Tests that previously dynamically import()'ed results are reference equal to
// require()'d results.
const common = require('../common');
const assert = require('assert');
const path = require('path');
const { pathToFileURL } = require('url');

(async () => {
  const modules = [
    '../fixtures/es-module-loaders/module-named-exports.mjs',
    '../fixtures/es-modules/import-esm.mjs',
    '../fixtures/es-modules/require-cjs.mjs',
    '../fixtures/es-modules/cjs-exports.mjs',
    '../common/index.mjs',
    '../fixtures/es-modules/package-type-module/index.js',
  ];
  for (const id of modules) {
    const url = pathToFileURL(path.resolve(__dirname, id));
    const imported = await import(url);
    const required = require(id);
    common.expectRequiredModule(required, imported);
  }

  const id = '../fixtures/es-modules/data-import.mjs';
  const imported = await import(id);
  const required = require(id);
  assert.strictEqual(imported.data, required.data);
})().then(common.mustCall());
