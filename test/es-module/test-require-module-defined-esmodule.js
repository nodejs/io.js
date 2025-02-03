// Flags: --experimental-require-module
'use strict';
const common = require('../common');

// If an ESM already defines __esModule to be something else,
// require(esm) should allow the user override.
{
  const mod = require('../fixtures/es-modules/export-es-module.mjs');
  common.expectRequiredModule(
    mod,
    { default: { hello: 'world' }, __esModule: 'test' },
    false,
  );
}

{
  const mod = require('../fixtures/es-modules/export-es-module-2.mjs');
  common.expectRequiredModule(
    mod,
    { default: { hello: 'world' }, __esModule: false },
    false,
  );
}
