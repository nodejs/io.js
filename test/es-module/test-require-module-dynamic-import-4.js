// Flags: --experimental-require-module
'use strict';

// This tests that previously asynchronously loaded submodule can still
// be loaded by require().

const common = require('../common');

(async () => {
  const imported = await import('../fixtures/es-modules/require-and-import/load.mjs');
  const required = require('../fixtures/es-modules/require-and-import/load.cjs');
  common.expectRequiredModule(required, imported);
})().then(common.mustCall());
