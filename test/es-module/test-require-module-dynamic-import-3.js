// Flags: --experimental-require-module
'use strict';

// This tests that previously synchronously loaded submodule can still
// be loaded by dynamic import().

const common = require('../common');

(async () => {
  const required = require('../fixtures/es-modules/require-and-import/load.cjs');
  const imported = await import('../fixtures/es-modules/require-and-import/load.mjs');
  common.expectRequiredModule(required, imported);
})().then(common.mustCall());
