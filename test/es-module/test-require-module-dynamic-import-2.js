// Flags: --experimental-require-module
'use strict';

// Tests that previously dynamically require()'ed results are reference equal to
// import()'d results.
const common = require('../common');
const assert = require('assert');
const { pathToFileURL } = require('url');
const path = require('path');

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
    const required = require(id);
    const imported = await import(url);
    common.expectRequiredModule(required, imported);
  }

  const id = '../fixtures/es-modules/data-import.mjs';
  const required = require(id);
  const imported = await import(id);
  assert.strictEqual(imported.data, required.data);
})().then(common.mustCall());
